// SPDX-License-Identifier: GPL-2.0
/*
 * bme680_i2c.c - I2C driver for Bosch BME680 environmental sensor
 *
 * Copyright (C) 2025 Your Name <your.email@example.com>
 *
 * This driver provides I2C communication for the BME680 sensor, interfacing
 * with the core driver (bme680.c) using regmap. It supports Device Tree probing,
 * lockdep for synchronization, and robust error handling.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <linux/lockdep.h>
#include "bme680.h"

#define BME680_I2C_ADDRESS_DEFAULT 0x77

/* Lockdep class for BME680 I2C mutex */
static DEFINE_MUTEX(bme680_i2c_lock);
static struct lock_class_key bme680_i2c_lock_key;

/* I2C regmap configuration */
static const struct regmap_config bme680_i2c_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = 0xFF,
};

/* I2C read operation with lockdep */
static int bme680_i2c_reg_read(void *context, unsigned int reg, unsigned int *val)
{
    struct i2c_client *client = context;
    int ret;

    lockdep_assert_held(&bme680_i2c_lock);

    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to read register 0x%02x: %d\n", reg, ret);
        return ret;
    }

    *val = ret;
    return 0;
}

/* I2C write operation with lockdep */
static int bme680_i2c_reg_write(void *context, unsigned int reg, unsigned int val)
{
    struct i2c_client *client = context;
    int ret;

    lockdep_assert_held(&bme680_i2c_lock);

    ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to write register 0x%02x: %d\n", reg, ret);
        return ret;
    }

    return 0;
}

/* I2C probe function */
static int bme680_i2c_probe(struct i2c_client *client,
                           const struct i2c_device_id *id)
{
    struct bme680_data *data;
    struct regmap *regmap;
    int ret;

    /* Initialize lockdep for I2C mutex */
    lockdep_init_map(&bme680_i2c_lock.dep_map, "bme680_i2c_lock",
                     &bme680_i2c_lock_key, 0);

    /* Allocate driver data */
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    /* Initialize regmap for I2C communication */
    regmap = devm_regmap_init(&client->dev, NULL, client,
                              &bme680_i2c_regmap_config);
    if (IS_ERR(regmap)) {
        dev_err(&client->dev, "Failed to initialize regmap: %ld\n",
                PTR_ERR(regmap));
        return PTR_ERR(regmap);
    }

    /* Set up lockdep-protected I2C operations */
    mutex_lock(&bme680_i2c_lock);
    data->regmap = regmap;
    i2c_set_clientdata(client, data);

    /* Initialize core driver */
    ret = bme680_core_probe(&client->dev, regmap, client->irq);
    if (ret) {
        dev_err(&client->dev, "Core probe failed: %d\n", ret);
        mutex_unlock(&bme680_i2c_lock);
        return ret;
    }

    mutex_unlock(&bme680_i2c_lock);

    dev_info(&client->dev, "BME680 I2C driver probed at address 0x%02x\n",
             client->addr);
    return 0;
}

/* I2C remove function */
static int bme680_i2c_remove(struct i2c_client *client)
{
    mutex_lock(&bme680_i2c_lock);
    bme680_core_remove(&client->dev);
    mutex_unlock(&bme680_i2c_lock);

    dev_info(&client->dev, "BME680 I2C driver removed\n");
    return 0;
}

/* I2C suspend function (power management) */
static int bme680_i2c_suspend(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct bme680_data *data = i2c_get_clientdata(client);

    mutex_lock(&bme680_i2c_lock);
    bme680_core_suspend(data);
    mutex_unlock(&bme680_i2c_lock);

    return 0;
}

/* I2C resume function (power management) */
static int bme680_i2c_resume(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct bme680_data *data = i2c_get_clientdata(client);

    mutex_lock(&bme680_i2c_lock);
    bme680_core_resume(data);
    mutex_unlock(&bme680_i2c_lock);

    return 0;
}

/* Device power management operations */
static const struct dev_pm_ops bme680_i2c_pm_ops = {
    .suspend = bme680_i2c_suspend,
    .resume = bme680_i2c_resume,
};

/* I2C device ID table */
static const struct i2c_device_id bme680_i2c_id[] = {
    { "bme680", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, bme680_i2c_id);

/* Device Tree compatible table */
static const struct of_device_id bme680_i2c_of_match[] = {
    { .compatible = "bosch,bme680" },
    { }
};
MODULE_DEVICE_TABLE(of, bme680_i2c_of_match);

/* I2C driver structure */
static struct i2c_driver bme680_i2c_driver = {
    .driver = {
        .name = "bme680_i2c",
        .of_match_table = bme680_i2c_of_match,
        .pm = &bme680_i2c_pm_ops,
    },
    .probe = bme680_i2c_probe,
    .remove = bme680_i2c_remove,
    .id_table = bme680_i2c_id,
};

/* Module initialization */
static int __init bme680_i2c_init(void)
{
    return i2c_add_driver(&bme680_i2c_driver);
}
module_init(bme680_i2c_init);

/* Module cleanup */
static void __exit bme680_i2c_exit(void)
{
    i2c_del_driver(&bme680_i2c_driver);
}
module_exit(bme680_i2c_exit);

MODULE_AUTHOR("Nguyen Trung Nhan");
MODULE_DESCRIPTION("I2C driver for Bosch BME680 environmental sensor");
MODULE_LICENSE("GPL v2");
