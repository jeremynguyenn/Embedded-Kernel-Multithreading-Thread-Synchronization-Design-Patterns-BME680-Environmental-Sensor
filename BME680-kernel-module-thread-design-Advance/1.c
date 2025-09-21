#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <mqueue.h>
#include <semaphore.h>
#include <string.h>
#include <time.h>
#include "bme680.h"
#include "thread_pool.h"
#include "monitor.h"
#include "pubsub.h"
#include "logger.h"
#include "bme680_config.h"

#define SHM_NAME "/bme680_shm"
#define MQ_NAME "/bme680_mq"
#define SEM_NAME "/bme680_sem"
#define NETLINK_USER 31
#define GAS_THRESHOLD 100000

struct bme680_sysv_msg {
    long mtype;
    struct bme680_fifo_data data;
};

struct task_arg {
    int num_reads;
    int dev_fd;
    int nl_sock;
    int sysv_msgid;
    mqd_t mq;
    sem_t *sem;
    struct bme680_monitor *monitor;
};

volatile sig_atomic_t keep_running = 1;
struct bme680_fifo_data *shared_data;
int shm_fd, sysv_shmid, sysv_semid, sysv_msgid;
struct bme680_monitor data_monitor;

void cleanup_handler(void *arg) {
    struct task_arg *targ = (struct task_arg *)arg;
    if (targ->dev_fd >= 0) close(targ->dev_fd);
    if (targ->nl_sock >= 0) close(targ->nl_sock);
    if (targ->mq != (mqd_t)-1) mq_close(targ->mq);
    if (targ->sem != SEM_FAILED) sem_close(targ->sem);
}

void producer_task(void *arg) {
    struct task_arg *targ = (struct task_arg *)arg;
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_cleanup_push(cleanup_handler, targ);

    int num_reads = targ->num_reads;
    while (keep_running && (num_reads == 0 || num_reads-- > 0)) {
        struct bme680_fifo_data fdata;
        if (ioctl(targ->dev_fd, BME680_IOC_READ_FIFO, &fdata) >= 0) {
            bme680_monitor_write(targ->monitor, &fdata);
            pubsub_publish("sensor_data", &fdata, sizeof(fdata));
            logger_log(LOG_INFO, "Produced data: Temp=%.2f°C, Press=%.2fhPa, Hum=%.2f%%, Gas=%uOhms",
                       fdata.temperature / 100.0, fdata.pressure / 100.0,
                       fdata.humidity / 1000.0, fdata.gas_resistance);
            if (mq_send(targ->mq, (char *)&fdata, sizeof(fdata), 0) < 0) {
                logger_log(LOG_ERROR, "mq_send failed: %s", strerror(errno));
            }
            struct bme680_sysv_msg sysv_msg = { .mtype = 1, .data = fdata };
            if (msgsnd(targ->sysv_msgid, &sysv_msg, sizeof(sysv_msg.data), IPC_NOWAIT) < 0) {
                logger_log(LOG_ERROR, "System V msgsnd failed: %s", strerror(errno));
            }
        }
        usleep(bme680_config_get_interval() * 1000);
        pthread_testcancel();
    }

    pthread_cleanup_pop(1);
}

void consumer_task(void *arg) {
    struct task_arg *targ = (struct task_arg *)arg;
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    while (keep_running) {
        struct bme680_fifo_data fdata;
        if (bme680_monitor_read(targ->monitor, &fdata)) {
            sem_wait(targ->sem);
            logger_log(LOG_INFO, "Consumed data: Temp=%.2f°C, Press=%.2fhPa, Hum=%.2f%%, Gas=%uOhms",
                       fdata.temperature / 100.0, fdata.pressure / 100.0,
                       fdata.humidity / 1000.0, fdata.gas_resistance);
            sem_post(targ->sem);
        }
        pthread_testcancel();
    }
}

void netlink_task(void *arg) {
    struct task_arg *targ = (struct task_arg *)arg;
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_cleanup_push(cleanup_handler, targ);

    struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
    targ->nl_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_USER);
    if (targ->nl_sock < 0) {
        logger_log(LOG_ERROR, "netlink socket failed: %s", strerror(errno));
        return;
    }
    if (bind(targ->nl_sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        logger_log(LOG_ERROR, "netlink bind failed: %s", strerror(errno));
        close(targ->nl_sock);
        return;
    }

    char buf[256];
    while (keep_running) {
        ssize_t len = recv(targ->nl_sock, buf, sizeof(buf), 0);
        if (len > 0) {
            logger_log(LOG_INFO, "Netlink alert: %s", buf);
        }
        pthread_testcancel();
    }
    pthread_cleanup_pop(1);
}

void sysv_msg_task(void *arg) {
    struct task_arg *targ = (struct task_arg *)arg;
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    struct bme680_sysv_msg sysv_msg;
    while (keep_running) {
        if (msgrcv(targ->sysv_msgid, &sysv_msg, sizeof(sysv_msg.data), 1, IPC_NOWAIT) >= 0) {
            logger_log(LOG_INFO, "System V Message: Temp=%.2f°C, Press=%.2fhPa, Hum=%.2f%%, Gas=%uOhms",
                       sysv_msg.data.temperature / 100.0, sysv_msg.data.pressure / 100.0,
                       sysv_msg.data.humidity / 1000.0, sysv_msg.data.gas_resistance);
        }
        usleep(100000);
        pthread_testcancel();
    }
}

void sensor_data_handler(void *data, size_t size) {
    struct bme680_fifo_data *fdata = (struct bme680_fifo_data *)data;
    logger_log(LOG_INFO, "PubSub: Temp=%.2f°C, Press=%.2fhPa, Hum=%.2f%%, Gas=%uOhms",
               fdata->temperature / 100.0, fdata->pressure / 100.0,
               fdata->humidity / 1000.0, fdata->gas_resistance);
}

void sig_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        keep_running = 0;
    }
}

int main(int argc, char *argv[]) {
    int opt, num_reads = 0;
    bool use_sysfs = false;
    while ((opt = getopt(argc, argv, "i:s")) != -1) {
        switch (opt) {
            case 'i': num_reads = atoi(optarg); break;
            case 's': use_sysfs = true; break;
            default: fprintf(stderr, "Usage: %s [-i num_reads] [-s]\n", argv[0]); exit(1);
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Initialize subsystems
    if (bme680_config_init("bme680.conf") < 0) {
        fprintf(stderr, "Failed to initialize config\n");
        return 1;
    }
    logger_init("bme680.log");
    thread_pool_init(bme680_config_get_thread_pool_size());
    bme680_monitor_init(&data_monitor);
    pubsub_init();
    pubsub_subscribe("sensor_data", sensor_data_handler);

    // Initialize IPC
    struct task_arg targ = {
        .num_reads = num_reads,
        .dev_fd = open("/dev/bme680", O_RDONLY | O_NONBLOCK),
        .nl_sock = -1,
        .sysv_msgid = msgget(1234, IPC_CREAT | 0666),
        .mq = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0666, &(struct mq_attr){.mq_maxmsg = 10, .mq_msgsize = sizeof(struct bme680_fifo_data)}),
        .sem = sem_open(SEM_NAME, O_CREAT, 0666, 1),
        .monitor = &data_monitor,
    };
    if (targ.dev_fd < 0 || targ.sysv_msgid < 0 || targ.mq == (mqd_t)-1 || targ.sem == SEM_FAILED) {
        logger_log(LOG_ERROR, "Failed to initialize IPC resources");
        goto cleanup;
    }

    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0 || ftruncate(shm_fd, sizeof(struct bme680_fifo_data)) < 0) {
        logger_log(LOG_ERROR, "Failed to initialize shared memory");
        goto cleanup;
    }
    shared_data = mmap(NULL, sizeof(struct bme680_fifo_data), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        logger_log(LOG_ERROR, "mmap failed: %s", strerror(errno));
        goto cleanup;
    }

    sysv_shmid = shmget(1234, sizeof(struct bme680_fifo_data), IPC_CREAT | 0666);
    sysv_semid = semget(1234, 1, IPC_CREAT | 0666);
    if (sysv_shmid < 0 || sysv_semid < 0) {
        logger_log(LOG_ERROR, "Failed to initialize System V IPC");
        goto cleanup;
    }

    // Enqueue tasks
    thread_pool_enqueue(producer_task, &targ);
    thread_pool_enqueue(consumer_task, &targ);
    thread_pool_enqueue(netlink_task, &targ);
    thread_pool_enqueue(sysv_msg_task, &targ);

    // Handle sysfs mode
    if (use_sysfs) {
        int interval_ms = bme680_config_get_interval();
        while (keep_running && (num_reads == 0 || num_reads-- > 0)) {
            FILE *temp_file = fopen("/sys/bus/iio/devices/iio:device0/in_temp_input", "r");
            FILE *press_file = fopen("/sys/bus/iio/devices/iio:device0/in_pressure_input", "r");
            FILE *humid_file = fopen("/sys/bus/iio/devices/iio:device0/in_humidityrelative_input", "r");
            FILE *gas_file = fopen("/sys/bus/iio/devices/iio:device0/in_resistance_input", "r");
            if (!temp_file || !press_file || !humid_file || !gas_file) {
                logger_log(LOG_ERROR, "Failed to open sysfs files");
                if (temp_file) fclose(temp_file);
                if (press_file) fclose(press_file);
                if (humid_file) fclose(humid_file);
                if (gas_file) fclose(gas_file);
                break;
            }
            float temp, press, humid, gas;
            fscanf(temp_file, "%f", &temp);
            fscanf(press_file, "%f", &press);
            fscanf(humid_file, "%f", &humid);
            fscanf(gas_file, "%f", &gas);
            logger_log(LOG_INFO, "SysFS - Temp=%.2f°C, Press=%.2fhPa, Hum=%.2f%%, Gas=%.0fOhms",
                       temp / 1000.0, press * 10.0, humid / 1000.0, gas);
            fclose(temp_file);
            fclose(press_file);
            fclose(humid_file);
            fclose(gas_file);
            usleep(interval_ms * 1000);
        }
    }

    // Wait for threads and cleanup
    thread_pool_shutdown();

cleanup:
    munmap(shared_data, sizeof(struct bme680_fifo_data));
    shm_unlink(SHM_NAME);
    close(shm_fd);
    shmctl(sysv_shmid, IPC_RMID, NULL);
    semctl(sysv_semid, 0, IPC_RMID);
    msgctl(targ.sysv_msgid, IPC_RMID, NULL);
    mq_unlink(MQ_NAME);
    sem_unlink(SEM_NAME);
    bme680_monitor_destroy(&data_monitor);
    pubsub_destroy();
    logger_destroy();
    bme680_config_destroy();
    logger_log(LOG_INFO, "Application terminated normally");
    return 0;
}