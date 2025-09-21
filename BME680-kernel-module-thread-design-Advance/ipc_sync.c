#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "ipc_sync.h"
#include "logger.h"
#include "bme680.h"

struct bme680_ipc_sync {
    int shmid;
    int semid;
    struct bme680_fifo_data *data;
};

static int sem_timedwait(int semid, struct timespec *ts) {
    struct sembuf sb = {0, -1, 0};
    return semtimedop(semid, &sb, 1, ts);
}

int ipc_sync_init(struct bme680_ipc_sync **ipc, key_t key) {
    *ipc = malloc(sizeof(struct bme680_ipc_sync));
    if (!*ipc) {
        logger_log(LOG_ERROR, "Failed to allocate IPC sync");
        return -ENOMEM;
    }

    (*ipc)->shmid = shmget(key, sizeof(struct bme680_fifo_data), IPC_CREAT | 0666);
    if ((*ipc)->shmid < 0) {
        logger_log(LOG_ERROR, "Failed to create shared memory: %s", strerror(errno));
        free(*ipc);
        return -errno;
    }

    (*ipc)->data = shmat((*ipc)->shmid, NULL, 0);
    if ((*ipc)->data == (void *)-1) {
        logger_log(LOG_ERROR, "Failed to attach shared memory: %s", strerror(errno));
        shmctl((*ipc)->shmid, IPC_RMID, NULL);
        free(*ipc);
        return -errno;
    }

    (*ipc)->semid = semget(key, 1, IPC_CREAT | 0666);
    if ((*ipc)->semid < 0) {
        logger_log(LOG_ERROR, "Failed to create semaphore: %s", strerror(errno));
        shmdt((*ipc)->data);
        shmctl((*ipc)->shmid, IPC_RMID, NULL);
        free(*ipc);
        return -errno;
    }

    union semun {
        int val;
        struct semid_ds *buf;
        unsigned short *array;
    } sem_arg;
    sem_arg.val = 1;
    if (semctl((*ipc)->semid, 0, SETVAL, sem_arg) < 0) {
        logger_log(LOG_ERROR, "Failed to initialize semaphore: %s", strerror(errno));
        shmdt((*ipc)->data);
        shmctl((*ipc)->shmid, IPC_RMID, NULL);
        semctl((*ipc)->semid, 0, IPC_RMID);
        free(*ipc);
        return -errno;
    }

    logger_log(LOG_INFO, "IPC sync initialized with key %d", key);
    return 0;
}

void ipc_sync_destroy(struct bme680_ipc_sync *ipc) {
    shmdt(ipc->data);
    shmctl(ipc->shmid, IPC_RMID, NULL);
    semctl(ipc->semid, 0, IPC_RMID);
    free(ipc);
    logger_log(LOG_INFO, "IPC sync destroyed");
}

int ipc_sync_write(struct bme680_ipc_sync *ipc, struct bme680_fifo_data *data) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5; // 5-second timeout

    if (data->temp < -40 || data->temp > 85 || data->pressure < 30000 || data->pressure > 110000 || data->humidity < 0 || data->humidity > 100) {
        logger_log(LOG_ERROR, "Invalid sensor data: temp=%f, pressure=%u, humidity=%u", data->temp, data->pressure, data->humidity);
        return -EINVAL;
    }

    if (sem_timedwait(ipc->semid, &ts) < 0) {
        logger_log(LOG_ERROR, "Semaphore wait failed: %s", strerror(errno));
        return -errno;
    }

    memcpy(ipc->data, data, sizeof(struct bme680_fifo_data));
    if (semop(ipc->semid, &(struct sembuf){0, 1, 0}, 1) < 0) {
        logger_log(LOG_ERROR, "Semaphore post failed: %s", strerror(errno));
        return -errno;
    }

    logger_log(LOG_DEBUG, "IPC sync write completed");
    return 0;
}

int ipc_sync_read(struct bme680_ipc_sync *ipc, struct bme680_fifo_data *data) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5; // 5-second timeout

    if (sem_timedwait(ipc->semid, &ts) < 0) {
        logger_log(LOG_ERROR, "Semaphore wait failed: %s", strerror(errno));
        return -errno;
    }

    memcpy(data, ipc->data, sizeof(struct bme680_fifo_data));
    if (data->temp < -40 || data->temp > 85 || data->pressure < 30000 || data->pressure > 110000 || data->humidity < 0 || data->humidity > 100) {
        logger_log(LOG_ERROR, "Invalid sensor data read: temp=%f, pressure=%u, humidity=%u", data->temp, data->pressure, data->humidity);
        semop(ipc->semid, &(struct sembuf){0, 1, 0}, 1);
        return -EINVAL;
    }

    if (semop(ipc->semid, &(struct sembuf){0, 1, 0}, 1) < 0) {
        logger_log(LOG_ERROR, "Semaphore post failed: %s", strerror(errno));
        return -errno;
    }

    logger_log(LOG_DEBUG, "IPC sync read completed");
    return 0;
}