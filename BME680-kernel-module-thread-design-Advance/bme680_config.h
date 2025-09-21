#ifndef BME680_CONFIG_H
#define BME680_CONFIG_H

#include "bme680.h"
#include "rwlock.h"
#define BME680_I2C_ADDRESS_DEFAULT 0x77
struct bme680_config {
    struct bme680_dev *dev;
    rwlock_t rwlock;
};

int bme680_config_init(struct bme680_config **config, struct bme680_dev *dev);
void bme680_config_destroy(struct bme680_config *config);
int bme680_config_set_temp_oversampling(struct bme680_config *config, uint8_t os);
int bme680_config_set_pressure_oversampling(struct bme680_config *config, uint8_t os);
int bme680_config_set_humidity_oversampling(struct bme680_config *config, uint8_t os);
int bme680_config_set_oversampling(u8 temp, u8 press, u8 hum);
int bme680_config_get_oversampling(u8 *temp, u8 *press, u8 *hum);
int bme680_config_set_i2c_address(u8 address);
int bme680_config_get_i2c_address(u8 *address);

#endif /* BME680_CONFIG_H */