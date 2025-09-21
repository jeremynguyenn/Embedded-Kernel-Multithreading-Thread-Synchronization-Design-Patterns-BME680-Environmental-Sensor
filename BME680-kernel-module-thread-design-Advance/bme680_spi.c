// SPDX-License-Identifier: GPL-2.0
/*
 * BME680 - SPI Driver
 *
 */
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/lockdep.h> // Thêm lockdep

#include "bme680.h"
#include <linux/of_device.h>
struct bme680_spi_bus_context {
    struct spi_device *spi;
    u8 current_page;
};
/* Thêm biến mutex và lockdep key */
static DEFINE_MUTEX(bme680_spi_lock);
static struct lock_class_key bme680_spi_lock_key;
/*
 * In SPI mode there are only 7 address bits, a "page" register determines
 * which part of the 8-bit range is active. This function looks at the address
 * and writes the page selection bit if needed
 */
static int bme680_regmap_spi_select_page(
    struct bme680_spi_bus_context *ctx, u8 reg)
{
    struct spi_device *spi = ctx->spi;
    int ret;
    u8 buf[2];
    u8 page = (reg & 0x80) ? 0 : 1; /* Page "1" is low range */

    if (page == ctx->current_page)
        return 0;

    /*
     * Data sheet claims we're only allowed to change bit 4, so we must do
     * a read-modify-write on each and every page select
     */
    buf[0] = BME680_REG_STATUS;
    ret = spi_write_then_read(spi, txbuf: buf, n_tx: 1, rxbuf: buf + 1, n_rx: 1);
    if (ret < 0) {
        dev_err(&spi->dev, "failed to set page %u\n", page);
        return ret;
    }

    buf[0] = BME680_REG_STATUS;
    if (page)
        buf[1] |= BME680_SPI_MEM_PAGE_BIT;
    else
        buf[1] &= ~BME680_SPI_MEM_PAGE_BIT;

    ret = spi_write(spi, buf, len: 2);
    if (ret < 0) {
        dev_err(&spi->dev, "failed to set page %u\n", page);
        return ret;
    }

    ctx->current_page = page;

    return 0;
}

/* Thêm cấu hình regmap */
static const struct regmap_config bme680_spi_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = 0xFF,
    .use_single_read = true,
    .use_single_write = true,
};

static int bme680_regmap_spi_write(void *context, const void *data,
    size_t count)
{
    struct bme680_spi_bus_context *ctx = context;
    struct spi_device *spi = ctx->spi;
    int ret;
    u8 buf[2];

    memcpy(buf, data, 2);

    ret = bme680_regmap_spi_select_page(ctx, reg: buf[0]);
    if (ret)
        return ret;

    /*
     * The SPI register address (= full register address without bit 7)
     * and the write command (bit7 = RW = '0')
     */
    buf[0] &= ~0x80;

    return spi_write(spi, buf, len: 2);
}

static int bme680_regmap_spi_read(void *context, const void *reg,
    size_t reg_size, void *val, size_t val_size)
{
    struct bme680_spi_bus_context *ctx = context;
    struct spi_device *spi = ctx->spi;
    int ret;
    u8 addr = *(const u8 *)reg;

    ret = bme680_regmap_spi_select_page(ctx, reg: addr);
    if (ret)
        return ret;

    addr |= 0x80; /* bit7 = RW = '1' */

    return spi_write_then_read(spi, txbuf: &addr, n_tx: 1, rxbuf: val, n_rx: val_size);
}
static int bme680_spi_reg_read(void *context, unsigned int reg, unsigned int *val)
{
    struct bme680_spi_bus_context *ctx = context;
    struct spi_device *spi = ctx->spi;
    u8 tx_buf[2] = {reg | 0x80, 0};
    u8 rx_buf[2];
    int ret;

    lockdep_assert_held(&bme680_spi_lock);

    ret = spi_write_then_read(spi, tx_buf, 1, rx_buf, 1);
    if (ret < 0) {
        dev_err(&spi->dev, "Failed to read register 0x%02x: %d\n", reg, ret);
        return ret;
    }

    *val = rx_buf[0];
    return 0;
}

/* Thêm hàm bme680_spi_reg_write */
static int bme680_spi_reg_write(void *context, unsigned int reg, unsigned int val)
{
    struct bme680_spi_bus_context *ctx = context;
    struct spi_device *spi = ctx->spi;
    u8 buf[2] = {reg & ~0x80, val};
    int ret;

    lockdep_assert_held(&bme680_spi_lock);

    ret = spi_write(spi, buf, 2);
    if (ret < 0) {
        dev_err(&spi->dev, "Failed to write register 0x%02x: %d\n", reg, ret);
        return ret;
    }

    return 0;
}

/* Thêm hàm bme680_spi_remove */
static int bme680_spi_remove(struct spi_device *spi)
{
    return bme680_core_remove(&spi->dev);
}

/* Thêm hàm bme680_spi_suspend */
static int bme680_spi_suspend(struct device *dev)
{
    struct spi_device *spi = to_spi_device(dev);
    struct bme680_data *data = spi_get_drvdata(spi);
    mutex_lock(&bme680_spi_lock);
    bme680_core_suspend(data);
    mutex_unlock(&bme680_spi_lock);
    return 0;
}

/* Thêm hàm bme680_spi_resume */
static int bme680_spi_resume(struct device *dev)
{
    struct spi_device *spi = to_spi_device(dev);
    struct bme680_data *data = spi_get_drvdata(spi);
    mutex_lock(&bme680_spi_lock);
    bme680_core_resume(data);
    mutex_unlock(&bme680_spi_lock);
    return 0;
}

/* Thêm cấu trúc dev_pm_ops */
static const struct dev_pm_ops bme680_spi_pm_ops = {
    .suspend = bme680_spi_suspend,
    .resume = bme680_spi_resume,
};

static const struct regmap_bus bme680_regmap_bus = {
    .write = bme680_regmap_spi_write,
    .read = bme680_regmap_spi_read,
    .reg_format_endian_default = REGMAP_ENDIAN_BIG,
    .val_format_endian_default = REGMAP_ENDIAN_BIG,
};

static int bme680_spi_probe(struct spi_device *spi)
{
    const struct spi_device_id *id = spi_get_device_id(sdev: spi);
    struct bme680_spi_bus_context *bus_context;
    struct regmap *regmap;

    bus_context = devm_kzalloc(dev: &spi->dev, size: sizeof(*bus_context), GFP_KERNEL);
    if (!bus_context)
        return -ENOMEM;

    bus_context->spi = spi;
    bus_context->current_page = 0xff; /* Undefined on warm boot */

    regmap = devm_regmap_init(dev: &spi->dev, bus: &bme680_regmap_bus, client: bus_context, config: &bme680_regmap_config);
    if (IS_ERR(regmap)) {
        dev_err(&spi->dev, "Failed to register spi regmap %ld\n", PTR_ERR(regmap));
        return PTR_ERR(regmap);
    }

    return bme680_core_probe(&spi->dev, regmap, id->name);
}

static const struct spi_device_id bme680_spi_id[] = {
    { "bme680", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, bme680_spi_id);

static const struct of_device_id bme680_of_match[] = {
    { .compatible = "bosch,bme680" },
    { }
};
MODULE_DEVICE_TABLE(of, bme680_of_match);

static struct spi_driver bme680_spi_driver = {
    .driver = {
        .name = "bme680_spi",
        .of_match_table = bme680_of_match,
        .pm = pm_ptr(&bme680_dev_pm_ops),
    },
    .probe = bme680_spi_probe,
    .id_table = bme680_spi_id,
};
module_spi_driver(bme680_spi_driver);

MODULE_AUTHOR("Nguyen Nhan");
MODULE_DESCRIPTION("BME680 SPI driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_BME680");