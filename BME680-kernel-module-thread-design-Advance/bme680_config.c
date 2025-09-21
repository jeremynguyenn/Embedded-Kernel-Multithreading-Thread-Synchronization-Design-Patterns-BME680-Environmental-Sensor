#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <sched.h>
#include "bme680_config.h"
#include "logger.h"

struct bme680_config {
    struct bme680_dev *dev;
    rwlock_t rwlock;
};

int bme680_config_init(struct bme680_config **config, struct bme680_dev *dev) {
    *config = malloc(sizeof(struct bme680_config));
    if (!*config) {
        logger_log(LOG_ERROR, "Failed to allocate config");
        return -ENOMEM;
    }
    (*config)->dev = dev;
    rwlock_init(&(*config)->rwlock);
    logger_log(LOG_INFO, "BME680 config initialized");
    return 0;
}

void bme680_config_destroy(struct bme680_config *config) {
    rwlock_destroy(&config->rwlock);
    free(config);
    logger_log(LOG_INFO, "BME680 config destroyed");
}

int bme680_config_set_temp_oversampling(struct bme680_config *config, uint8_t os) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(sched_getcpu() % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    rwlock_wrlock(&config->rwlock);
    int ret = bme680_set_sensor_settings(config->dev, BME680_OS_TEMP, os);
    rwlock_unlock(&config->rwlock);
    if (ret == 0) {
        logger_log(LOG_DEBUG, "Temperature oversampling set to %u", os);
    } else {
        logger_log(LOG_ERROR, "Failed to set temperature oversampling: %d", ret);
    }
    return ret;
}

int bme680_config_set_pressure_oversampling(struct bme680_config *config, uint8_t os) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(sched_getcpu() % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    rwlock_wrlock(&config->rwlock);
    int ret = bme680_set_sensor_settings(config->dev, BME680_OS_PRESS, os);
    rwlock_unlock(&config->rwlock);
    if (ret == 0) {
        logger_log(LOG_DEBUG, "Pressure oversampling set to %u", os);
    } else {
        logger_log(LOG_ERROR, "Failed to set pressure oversampling: %d", ret);
    }
    return ret;
}

int bme680_config_set_humidity_oversampling(struct bme680_config *config, uint8_t os) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(sched_getcpu() % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    rwlock_wrlock(&config->rwlock);
    int ret = bme680_set_sensor_settings(config->dev, BME680_OS_HUM, os);
    rwlock_unlock(&config->rwlock);
    if (ret == 0) {
        logger_log(LOG_DEBUG, "Humidity oversampling set to %u", os);
    } else {
        logger_log(LOG_ERROR, "Failed to set humidity oversampling: %d", ret);
    }
    return ret;
}