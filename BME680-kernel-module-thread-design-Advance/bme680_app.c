#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include "bme680.h"
#include "thread_pool.h"
#include "monitor.h"
#include "pubsub.h"
#include "logger.h"
#include "timer.h"
#include "event_pair.h"
#include "fifo_semaphore.h"
#include "assembly_line.h"
#include "bme680_config.h"
#include "fork_handler.h"
#include "ipc_sync.h"
#include "rwlock.h"
#include "recursive_mutex.h"
#include "deadlock_detector.h"
#include "dining_philosophers.h"
#include "barrier.h"
#include <time.h>
#include <signal.h>
// Định nghĩa structs và functions để tích hợp kernel interaction (user-space app gọi kernel module qua /dev/i2c or char device)
struct bme680_dev {
    int fd; // File descriptor cho /dev/i2c-1 hoặc /dev/bme680
    uint8_t addr; // I2C address
    struct bme680_config config; // Config từ bme680_config.h
};
/* Thêm các biến toàn cục */
static assembly_line_t *assembly_line;
static thread_pool_t *thread_pool;
static bme680_monitor_t *monitor;
static timer_t *timer;
static fork_handler_t *fork_handler;
static ipc_sync_t *ipc_sync;
static barrier_t *barrier;
static dining_philosophers_t *dining_philosophers;
static rwlock_t *rwlock;
static recursive_mutex_t *recursive_mutex;
static deadlock_detector_t *deadlock_detector;
static fifo_semaphore_t *fifo_semaphore;
static event_pair_t *event_pair;
static int fd;

static int bme680_dev_init(const char *dev_path, uint8_t addr) {
    struct bme680_dev *dev = malloc(sizeof(struct bme680_dev));
    if (!dev) return -ENOMEM;
    dev->fd = open(dev_path, O_RDWR);
    if (dev->fd < 0) {
        logger_log(LOG_ERROR, "Failed to open device %s: %s", dev_path, strerror(errno));
        free(dev);
        return -ENODEV;
    }
    dev->addr = addr;
    if (ioctl(dev->fd, I2C_SLAVE, addr) < 0) {
        logger_log(LOG_ERROR, "Failed to set I2C slave address: %s", strerror(errno));
        close(dev->fd);
        free(dev);
        return -EIO;
    }
    // Init config
    bme680_config_init(&dev->config);
    logger_log(LOG_INFO, "BME680 device initialized on %s addr 0x%x", dev_path, addr);
    return (int)dev; // Trả về pointer cast to int nếu cần, nhưng dùng pointer
}

static void bme680_dev_destroy(struct bme680_dev *dev) {
    if (dev) {
        close(dev->fd);
        free(dev);
        logger_log(LOG_INFO, "BME680 device destroyed");
    }
}

static int bme680_read_sensor(struct bme680_dev *dev, struct bme680_fifo_data *data) {
    // Đọc từ I2C
    uint8_t reg = BME680_REG_TEMP_MSB;
    uint8_t buf[8];
    if (i2c_smbus_read_i2c_block_data(dev->fd, reg, 8, buf) < 0) {
        logger_log(LOG_ERROR, "Failed to read sensor data: %s", strerror(errno));
        return -EIO;
    }
    // Parse data (giả sử, full parse từ kernel logic)
    data->temp = (float)((buf[0] << 12 | buf[1] << 4 | buf[2] >> 4) / 16.0); // Simulate parse
    data->pressure = buf[3] << 12 | buf[4] << 4 | buf[5] >> 4;
    data->humidity = buf[6] << 8 | buf[7];
    data->gas_resistance = 0; // Giả sử, thêm nếu cần
    return 0;
}

// App struct đầy đủ tích hợp tất cả patterns
struct bme680_app {
    struct bme680_dev *dev;
    struct thread_pool *tp;
    struct bme680_monitor *monitor;
    struct assembly_line *al;
    fifo_semaphore_t *sem;
    event_pair_t *ep;
    timer_t *timer;
    fork_handler_t *fh;
    ipc_sync_t *ipc_sync;
    rwlock_t *rwlock;
    recursive_mutex_t *rmutex;
    deadlock_detector_t *dd;
    dining_philosophers_t *dp;
    barrier_t *barrier;
    volatile int running;
};

// Cleanup handler cho app
static void app_cleanup(void *arg) {
    struct bme680_app *app = (struct bme680_app *)arg;
    logger_log(LOG_INFO, "App cleanup: destroying all resources");
    fork_handler_destroy(app->fh);
    ipc_sync_destroy(app->ipc_sync);
    rwlock_destroy(app->rwlock);
    recursive_mutex_destroy(app->rmutex);
    deadlock_detector_destroy(app->dd);
    dining_philosophers_destroy(app->dp);
    barrier_destroy(app->barrier);
    assembly_line_destroy(app->al);
    timer_destroy(app->timer);
    event_pair_destroy(app->ep);
    fifo_semaphore_destroy(app->sem);
    bme680_monitor_destroy(app->monitor);
    thread_pool_destroy(app->tp);
    bme680_dev_destroy(app->dev);
    pubsub_destroy();
    logger_destroy();
}

/* Thêm hàm signal_handler */
static void signal_handler(int sig) {
    logger_log(LOG_INFO, "Received signal %d, shutting down", sig);
    app.running = 0;
}

static void event_loop(void)
{
    struct bme680_fifo_data data;
    while (1) {
        fifo_semaphore_wait(fifo_semaphore);
        bme680_monitor_read(monitor, &data);
        thread_pool_enqueue(thread_pool, process_data, &data);
        pubsub_publish("sensor_data", &data, sizeof(data));
    }
}

static void process_data(void *arg) {
    struct bme680_fifo_data *data = (struct bme680_fifo_data *)arg;
    char msg[128];
    snprintf(msg, sizeof(msg), "Temp: %.2f C, Pressure: %u Pa, Humidity: %u%%, Gas: %u Ohms",
             data->temp, data->pressure, data->humidity, data->gas_resistance);
    pubsub_publish("sensor_data", msg, strlen(msg) + 1);
    free(data);
}

static void read_sensor(void *arg) {
    struct bme680_app *app = (struct bme680_app *)arg;
    struct bme680_fifo_data data;
    if (bme680_read_sensor(app->dev, &data) == 0) {
        if (fifo_semaphore_wait(app->sem) == 0) {
            if (bme680_monitor_write(app->monitor, &data) == 0) {
                event_pair_signal1(app->ep);
            } else {
                logger_log(LOG_ERROR, "Failed to write to monitor");
            }
            fifo_semaphore_post(app->sem);
        } else {
            logger_log(LOG_ERROR, "Failed to acquire FIFO semaphore");
        }
    } else {
        logger_log(LOG_ERROR, "Failed to read sensor data");
    }
}

static void *event_loop(void *arg) {
    struct bme680_app *app = (struct bme680_app *)arg;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(sched_getcpu() % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    pthread_cleanup_push(app_cleanup, app);
    while (app->running) {
        struct bme680_fifo_data data;
        if (bme680_monitor_read(app->monitor, &data) == 0) {
            event_pair_wait1(app->ep);
            if (assembly_line_process(app->al, &data) == 0) {
                assembly_line_get_result(app->al, &data);
                thread_pool_enqueue(app->tp, process_data, &data);
            }
            event_pair_signal2(app->ep);
        } else {
            logger_log(LOG_ERROR, "Failed to read from monitor");
        }
        usleep(100000); // Avoid busy loop
    }
    pthread_cleanup_pop(0);
    return NULL;
}

// Function test assembly line với valid/invalid data
static void test_assembly_line(struct bme680_app *app, int num_stages, int iterations, int invalid) {
    for (int i = 0; i < iterations; i++) {
        struct bme680_fifo_data data = { .temp = 25.0 + i, .pressure = 101325 + i, .humidity = 50 + i, .gas_resistance = 100000 + i };
        if (invalid) {
            data.temp = -100; // Invalid to test invariants
        }
        assembly_line_process(app->al, &data);
        assembly_line_get_result(app->al, &data);
        logger_log(LOG_INFO, "Test iteration %d: Temp %.2f", i, data.temp);
    }
}

int main(int argc, char *argv[]) {
    struct bme680_app app;
    int iterations = 10;
    int threads = 4;
    int num_stages = 5;
    int run_tests = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            num_stages = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--test") == 0) {
            run_tests = 1;
        }
    }

    logger_init("bme680.log");
    logger_set_level(LOG_DEBUG);
    pubsub_init();
    thread_pool_init(&app.tp, threads);
    bme680_monitor_init(&app.monitor, 100);
    fifo_semaphore_init(&app.sem, 1);
    event_pair_init(&app.ep);
    timer_init(&app.timer, 1000, read_sensor, &app); // Read sensor every 1s
    if (assembly_line_init(&app.al, num_stages) != 0) {
        logger_log(LOG_ERROR, "Failed to initialize assembly line");
        goto cleanup;
    }
    // Tích hợp thêm patterns để expert
    fork_handler_init(&app.fh, threads);
    ipc_sync_init(&app.ipc_sync, 1234);
    rwlock_init(&app.rwlock);
    recursive_mutex_init(&app.rmutex);
    deadlock_detector_init(&app.dd, 10); // Giả sử 10 mutexes
    dining_philosophers_init(&app.dp, num_stages);
    barrier_init(&app.barrier, threads);

    app.dev = (struct bme680_dev *)bme680_dev_init("/dev/i2c-1", 0x77);
    if (!app.dev) {
        logger_log(LOG_ERROR, "Failed to initialize BME680 device");
        goto cleanup;
    }
    app.running = 1;

    pthread_t event_thread;
    if (pthread_create(&event_thread, NULL, event_loop, &app) != 0) {
        logger_log(LOG_ERROR, "Failed to create event loop thread");
        goto cleanup;
    }

    if (run_tests) {
        // Run tests with valid and invalid data
        test_assembly_line(&app, num_stages, iterations, 0); // Valid data
        test_assembly_line(&app, num_stages, iterations, 1); // Invalid data
    } else {
        sleep(60); // Run for 60 seconds
    }

cleanup:
    app.running = 0;
    pthread_join(event_thread, NULL);
    app_cleanup(&app); // Gọi cleanup đầy đủ
    return 0;
}