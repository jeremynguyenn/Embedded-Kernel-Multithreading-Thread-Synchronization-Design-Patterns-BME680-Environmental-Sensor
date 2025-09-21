// SPDX-License-Identifier: GPL-2.0
/*
 * Bosch BME680 - Temperature, Pressure, Humidity & Gas Sensor
 *
 *
 * Datasheet:
 * [https://ae-bst.resource.bosch.com/media/_tech/media/datasheets/BST-BME680-DS001-00.pdf]
 */
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include <linux/unaligned.h>
#include <linux/lockdep.h> // Thêm cho lockdep

#include <linux/spinlock.h>
#include <linux/rwlock.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/timer.h>

#include "bme680.h"

/* 1st set of calibration data */
enum {
    /* Temperature calib indexes */
    T2_LSB = 0,
    T3 = 2,
    /* Pressure calib indexes */
    P1_LSB = 4,
    P2_LSB = 6,
    P3 = 8,
    P4_LSB = 10,
    P5_LSB = 12,
    P7 = 14,
    P6 = 15,
    P8_LSB = 18,
    P9_LSB = 20,
    P10 = 22,
};

/* 2nd set of calibration data */
enum {
    /* Humidity calib indexes */
    H2_MSB = 0,
    H1_LSB = 1,
    H3 = 3,
    H4 = 4,
    H5 = 5,
    H6 = 6,
    H7 = 7,
    /* Stray T1 calib index */
    T1_LSB = 8,
    /* Gas heater calib indexes */
    GH2_LSB = 10,
    GH1 = 12,
    GH3 = 13,
};

/* 3rd set of calibration data */
enum {
    RES_HEAT_VAL = 0,
    RES_HEAT_RANGE = 2,
    RANGE_SW_ERR = 4,
};

struct bme680_calib {
    u16 par_t1;
    s16 par_t2;
    s8 par_t3;
    u16 par_p1;
    s16 par_p2;
    s8 par_p3;
    s16 par_p4;
    s16 par_p5;
    s8 par_p6;
    s8 par_p7;
    s16 par_p8;
    s16 par_p9;
    u8 par_p10;
    u16 par_h1;
    u16 par_h2;
    s8 par_h3;
    s8 par_h4;
    s8 par_h5;
    u8 par_h6;
    s8 par_h7;
    s8 par_gh1;
    s16 par_gh2;
    s8 par_gh3;
    u8 res_heat_range;
    s8 res_heat_val;
    s8 range_sw_err;
};

struct bme680_fifo_data {
    s32 temp;
    u32 pressure;
    u32 humidity;
    u32 gas_res;
};

static const char * const bme680_supply_names[] = { "vdd", "vddio" };

struct bme680_data {
    struct regmap *regmap;
    struct bme680_calib bme680;
    struct mutex lock; /* Protect multiple serial R/W ops to device. */
    u8 oversampling_temp;
    u8 oversampling_press;
    u8 oversampling_humid;
    u8 preheat_curr_mA;
    u16 heater_dur;
    u16 heater_temp;
    struct regulator_bulk_data supplies[ARRAY_SIZE(bme680_supply_names)];
    struct iio_trigger *trig;
    struct fwnode_handle *fwnode;
    bool powered;
    s32 temp_adc;
    u32 pressure_adc;
    u16 humid_adc;
    u32 gas_adc;
    u8 gas_range;
    ktime_t timestamp;
    struct completion completion;
    struct task_struct *poll_thread;
    wait_queue_head_t poll_wq;
    kfifo_declare(bme680_fifo, struct iio_poll_func, 1);
    lockdep_map lockdep_map; // Thêm cho lockdep
	
	struct device *dev;
    spinlock_t reg_lock;
    rwlock_t calib_lock;
    atomic_t read_count;
    atomic_t error_count;
    DECLARE_KFIFO(data_fifo, struct bme680_fifo_data, 1024);
    struct semaphore fifo_sem;
    struct timer_list threshold_timer;
    u32 threshold_temp;
    u32 threshold_press;
    u32 threshold_hum;
    u32 threshold_gas;
    u8 chip_id;
    u8 variant_id;
};
struct mutex bme680_i2c_lock;
enum bme680_op_mode {
    BME680_MODE_SLEEP = 0,
    BME680_MODE_FORCED = 1,
};

enum bme680_scan {
    BME680_TEMP,
    BME680_PRESS,
    BME680_HUMID,
    BME680_GAS,
};

static const struct iio_chan_spec bme680_channels[] = {
    {
        .type = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
        .info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
        .scan_index = BME680_TEMP,
        .scan_type = {
            .sign = 's',
            .realbits = 32,
            .storagebits = 32,
            .endianness = IIO_CPU,
        },
    },
    {
        .type = IIO_PRESSURE,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
        .info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
        .scan_index = BME680_PRESS,
        .scan_type = {
            .sign = 'u',
            .realbits = 32,
            .storagebits = 32,
            .endianness = IIO_CPU,
        },
    },
    {
        .type = IIO_HUMIDITYRELATIVE,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED) | BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
        .info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
        .scan_index = BME680_HUMID,
        .scan_type = {
            .sign = 'u',
            .realbits = 16,
            .storagebits = 16,
            .endianness = IIO_CPU,
        },
    },
    {
        .type = IIO_RESISTANCE,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_PROCESSED),
        .info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ) | BIT(IIO_CHAN_INFO_HEATER_TEMP) | BIT(IIO_CHAN_INFO_HEATER_DUR),
        .scan_index = BME680_GAS,
        .scan_type = {
            .sign = 'u',
            .realbits = 32,
            .storagebits = 32,
            .endianness = IIO_CPU,
        },
    },
    IIO_CHAN_SOFT_TIMESTAMP(4),
};

/* Thêm hàm bme680_poll_thread */
static int bme680_poll_thread(void *arg)
{
    struct bme680_data *data = arg;
    struct bme680_fifo_data fifo;
    while (!kthread_should_stop()) {
        mutex_lock(&data->lock);
        if (bme680_read_raw_data(data, &fifo) == 0) {
            down(&data->fifo_sem);
            kfifo_put(&data->data_fifo, fifo);
            up(&data->fifo_sem);
            complete(&data->data_ready);
        } else {
            atomic_inc(&data->error_count);
        }
        mutex_unlock(&data->lock);
        usleep_range(1000, 2000);
    }
    return 0;
}

/* Thêm hàm check_threshold_task */
static void check_threshold_task(struct timer_list *t)
{
    struct bme680_data *data = from_timer(data, t, threshold_timer);
    struct bme680_fifo_data fifo;
    int ret;

    down(&data->fifo_sem);
    ret = kfifo_get(&data->data_fifo, &fifo);
    up(&data->fifo_sem);
    if (ret) {
        if (fifo.temp > data->threshold_temp) bme680_netlink_send(data, "Temperature threshold exceeded");
        if (fifo.pressure > data->threshold_press) bme680_netlink_send(data, "Pressure threshold exceeded");
        if (fifo.humidity > data->threshold_hum) bme680_netlink_send(data, "Humidity threshold exceeded");
        if (fifo.gas_res > data->threshold_gas) bme680_netlink_send(data, "Gas threshold exceeded");
    }
    mod_timer(&data->threshold_timer, jiffies + HZ);
}

/* Thêm hàm bme680_compensate_temp */
static s32 bme680_compensate_temp(struct bme680_data *data, u32 adc_temp)
{
    s64 var1, var2, var3, calc_temp;

    var1 = ((s64) adc_temp) / 16384 - ((s64) data->bme680.par_t1) * 2;
    var2 = var1 * ((s64) data->bme680.par_t2);
    var3 = (var1 / 2) * (var1 / 2) * ((s64) data->bme680.par_t3) / 1024;
    data->bme680.t_fine = var2 + var3;
    calc_temp = (data->bme680.t_fine * 5 + 128) / 256;
    return calc_temp;
}

/* Thêm hàm bme680_compensate_press */
static u32 bme680_compensate_press(struct bme680_data *data, u32 adc_press)
{
    s64 var1, var2, var3, var4, calc_press;

    var1 = ((s64)data->bme680.t_fine / 2) - 64000;
    var2 = var1 * var1 * ((s64)data->bme680.par_p6) / 32768;
    var2 += var1 * ((s64)data->bme680.par_p5) * 2;
    var2 = (var2 / 4) + (((s64)data->bme680.par_p4) * 65536);
    var3 = (((s64)data->bme680.par_p3 * var1 * var1) / 524288 + ((s64)data->bme680.par_p2 * var1)) / 524288;
    var4 = (1 + var3 / 32768) * ((s64)data->bme680.par_p1);
    calc_press = 1048576 - adc_press;
    calc_press = (calc_press - (var2 / 4096)) * 6250 / var4;
    var1 = ((s64)data->bme680.par_p9 * calc_press * calc_press) / 2147483648;
    var2 = calc_press * ((s64)data->bme680.par_p8) / 32768;
    var3 = (calc_press / 256) * (calc_press / 256) * (calc_press / 256) * (data->bme680.par_p10 / 131072);
    calc_press += (var1 + var2 + var3 + ((s64)data->bme680.par_p7 * 128)) / 16;
    return calc_press;
}

/* Thêm hàm bme680_compensate_hum */
static u32 bme680_compensate_hum(struct bme680_data *data, u32 adc_hum)
{
    s64 calc_hum, var1, var2, var3, var4, temp_comp;

    temp_comp = ((s64)data->bme680.t_fine * 5 + 128) / 256;
    var1 = (s64)(adc_hum - ((s64)(data->bme680.par_h1 * 16))) - (((temp_comp * (s64)data->bme680.par_h3) / 100) >> 1);
    var2 = (s64)data->bme680.par_h2 * (((temp_comp * (s64)data->bme680.par_h4) / 100) + (((temp_comp * ((temp_comp * (s64)data->bme680.par_h5) / 100)) >> 6) / 100) + (1 << 14));
    var3 = var1 * var2;
    var4 = (((s64)data->bme680.par_h6 << 7) * ((temp_comp * (s64)data->bme680.par_h7) / 100)) >> 4;
    var3 += var4;
    calc_hum = (((var3 >> 14) * (var3 >> 14) * ((s64)data->bme680.par_h6) / 100) >> 1) / 4096;
    calc_hum = var3 >> 14 - calc_hum;
    calc_hum = (((calc_hum * 1000) >> 13) * 100) >> 12;
    if (calc_hum > 100000) calc_hum = 100000;
    if (calc_hum < 0) calc_hum = 0;
    return calc_hum;
}

/* Thêm hàm bme680_compensate_gas */
static u32 bme680_compensate_gas(struct bme680_data *data, u16 adc_gas, u8 const_array_idx)
{
    s64 var1, var2, var3, calc_gas_res;
    u8 const_array1 = (const_array_idx & 0x0F) * 2;
    u8 const_array2 = const_array1 + 1;
    const u8 const_array[32] = {0,0,0,0,0,-1,0,-3,0,0,0,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

    var1 = (s64)((1340 + (5 * (s64)data->bme680.range_sw_err)) * ((s64) const_array[const_array1])) / 65536;
    var2 = (((s64)((adc_gas * 32768) - (16777216))) * ((s64) const_array[const_array2])) / 4096;
    var3 = (((s64) data->bme680.res_heat_range * var2) / 4) + (var1 * 4);
    calc_gas_res = (s64)(5 * var3) / 100;
    return calc_gas_res;
}

/* Thêm hàm bme680_core_probe */
int bme680_core_probe(struct device *dev, struct regmap *regmap, const char *name)
{
    int ret;
    mutex_lock(&bme680_i2c_lock);
    ret = bme680_probe(dev, regmap, name);
    mutex_unlock(&bme680_i2c_lock);
    return ret;
}

/* Thêm hàm bme680_core_remove */
int bme680_core_remove(struct device *dev)
{
    struct bme680_data *data = iio_priv(to_iio_dev(dev));
    mutex_lock(&bme680_i2c_lock);
    del_timer_sync(&data->threshold_timer);
    kthread_stop(data->poll_thread);
    mutex_unlock(&bme680_i2c_lock);
    return 0;
}

/* Thêm hàm bme680_core_suspend */
int bme680_core_suspend(struct bme680_data *data)
{
    mutex_lock(&data->lock);
    mutex_unlock(&data->lock);
    return 0;
}

/* Thêm hàm bme680_core_resume */
int bme680_core_resume(struct bme680_data *data)
{
    mutex_lock(&data->lock);
    mutex_unlock(&data->lock);
    return 0;
}

static int bme680_read_calib(struct bme680_data *data, struct bme680_calib *calib)
{
    int ret;
    u8 buf[23];
    u8 buf2[14];
    u8 buf3[5];
    unsigned int tmp;

    ret = regmap_bulk_read(data->regmap, BME680_CALIB_ADDR1, buf, sizeof(buf));
    if (ret < 0) {
        dev_err(data->dev, "failed to read calib data addr1\n");
        return ret;
    }

    calib->par_t2 = get_unaligned_le16(buf + T2_LSB);
    calib->par_t3 = buf[T3];

    calib->par_p1 = get_unaligned_le16(buf + P1_LSB);
    calib->par_p2 = get_unaligned_le16(buf + P2_LSB);
    calib->par_p3 = buf[P3];
    calib->par_p4 = get_unaligned_le16(buf + P4_LSB);
    calib->par_p5 = get_unaligned_le16(buf + P5_LSB);
    calib->par_p6 = buf[P6];
    calib->par_p7 = buf[P7];
    calib->par_p8 = get_unaligned_le16(buf + P8_LSB);
    calib->par_p9 = get_unaligned_le16(buf + P9_LSB);
    calib->par_p10 = buf[P10];

    ret = regmap_bulk_read(data->regmap, BME680_CALIB_ADDR2, buf2, sizeof(buf2));
    if (ret < 0) {
        dev_err(data->dev, "failed to read calib data addr2\n");
        return ret;
    }

    calib->par_h2 = (buf2[H2_MSB] << 4) | ((buf2[H1_LSB] >> 4) & 0x0f);
    calib->par_h1 = (buf2[H1_LSB] << 4) | (buf2[H1_LSB] & 0x0f);
    calib->par_h3 = buf2[H3];
    calib->par_h4 = buf2[H4];
    calib->par_h5 = buf2[H5];
    calib->par_h6 = buf2[H6];
    calib->par_h7 = buf2[H7];
    calib->par_t1 = get_unaligned_le16(buf2 + T1_LSB);
    calib->par_gh2 = get_unaligned_le16(buf2 + GH2_LSB);
    calib->par_gh1 = buf2[GH1];
    calib->par_gh3 = buf2[GH3];

    ret = regmap_bulk_read(data->regmap, BME680_CALIB_ADDR3, buf3, sizeof(buf3));
    if (ret < 0) {
        dev_err(data->dev, "failed to read calib data addr3\n");
        return ret;
    }

    calib->res_heat_val = buf3[RES_HEAT_VAL];
    calib->res_heat_range = FIELD_GET(BME680_RSERROR_MASK, buf3[RES_HEAT_RANGE]);
    calib->range_sw_err = FIELD_GET(BME680_RSERROR_MASK, buf3[RANGE_SW_ERR]);

    return 0;
}

static s32 calc_temperature(struct bme680_data *data, s32 temp_adc)
{
    s64 var1;
    s64 var2;
    s64 var3;
    s32 calc_temp;

    var1 = ((s32)temp_adc >> 3) - ((s32)data->bme680.par_t1 << 1);
    var2 = (var1 * (s32)data->bme680.par_t2) >> 11;
    var3 = ((var1 >> 1) * (var1 >> 1)) >> 12;
    var3 = ((var3) * ((s32)data->bme680.par_t3 << 4)) >> 14;
    data->t_fine = (s32)(var2 + var3);
    calc_temp = (s16)(((data->t_fine * 5) + 128) >> 8);

    return calc_temp;
}

static u32 calc_pressure(struct bme680_data *data, u32 pressure_adc)
{
    s64 var1 = 0;
    s64 var2 = 0;
    s64 var3 = 0;
    s64 var4 = 0;
    s64 calc_press = 0;

    var1 = (((s64)data->t_fine) >> 1) - 64000;
    var2 = ((((var1 >> 2) * (var1 >> 2)) >> 11) * (s64)data->bme680.par_p6) >> 2;
    var2 = var2 + ((var1 * (s64)data->bme680.par_p5) << 1);
    var2 = (var2 >> 2) + ((s64)data->bme680.par_p4 << 16);
    var1 = (((((var1 >> 2) * (var1 >> 2)) >> 13) * ((s64)data->bme680.par_p3 << 5)) >> 3) + (((s64)data->bme680.par_p2 * var1) >> 1);
    var1 = var1 >> 18;
    var1 = ((32768 + var1) * (s64)data->bme680.par_p1) >> 15;
    calc_press = 1048576 - pressure_adc;
    calc_press = (unsigned int)(((calc_press - (var2 >> 12)) * ((s32)3125)));
    var4 = (1LL << 47);
    calc_press = div64_s64(calc_press, var1) * 1000;
    var1 = ((s64)data->bme680.par_p9 * (s64)(((calc_press >> 13) * (calc_press >> 13)) >> 12)) >> 25;
    var2 = ((s64)data->bme680.par_p8 * (s64)calc_press) >> 19;
    var3 = ((s64)data->bme680.par_p10 * (s64)calc_press) >> 48;
    calc_press = (s64)calc_press + ((var1 + var2 + var3 + ((s64)data->bme680.par_p7 << 7)) >> 4);

    return (u32)calc_press;
}

static u32 calc_humidity(struct bme680_data *data, u16 humid_adc)
{
    s32 var1;
    s32 var2;
    s32 var3;
    s32 var4;
    s32 var5;
    s32 var6;
    s32 temp_scaled;
    s32 calc_hum;

    temp_scaled = (((s32)data->t_fine * 5) + 128) >> 8;
    var1 = (s32)(humid_adc - ((s32)((s32)data->bme680.par_h1 * 16))) - (((temp_scaled * (s32)data->bme680.par_h3) / ((s32)100)) >> 1);
    var2 = ((s32)data->bme680.par_h2 * (((temp_scaled * (s32)data->bme680.par_h4) / ((s32)100)) + (((temp_scaled * ((temp_scaled * (s32)data->bme680.par_h5) / ((s32)100))) >> 6) / ((s32)100)) + (s32)(1 << 14))) >> 10;
    var3 = var1 * var2;
    var4 = (s32)data->bme680.par_h6 << 7;
    var4 = ((var4) + ((temp_scaled * (s32)data->bme680.par_h7) / ((s32)100))) >> 4;
    var5 = ((var3 >> 14) * (var3 >> 14)) >> 10;
    var6 = (var4 * var5) >> 1;
    calc_hum = (((var3 + var6) >> 10) * ((s32)1000)) >> 12;
    if (calc_hum > 100000) /* Cap at 100%rH */
        calc_hum = 100000;
    else if (calc_hum < 0)
        calc_hum = 0;

    return (u32)calc_hum;
}

static u32 calc_gas_resistance_low(struct bme680_data *data, u16 gas_adc, u8 gas_range)
{
    s64 var1;
    s64 var2;
    s64 var3;
    u32 calc_gas_res;

    var1 = (s64)((1340 + (5 * (s64)data->bme680.range_sw_err)) * ((s64)lookup_k[gas_range])) >> 16;
    var2 = (((s64)((s64)gas_adc << 15) - (s64)(16777216)) + var1);
    var3 = (((s64)lookup_m[gas_range] * (s64)var1) >> 9);
    calc_gas_res = (u32)((var3 + ((var2) >> 1)) / (var2));

    return calc_gas_res;
}

static u32 calc_gas_resistance_high(struct bme680_data *data, u16 gas_adc, u8 gas_range)
{
    u64 var1 = 0;
    u32 var2 = 0;
    u32 var3 = 0;
    u32 calc_gas_res;

    var2 = (u32)(((u32)262144) >> gas_range);
    var1 = (u64)(gas_adc) * (u64)512 - (u64)512;
    var3 = (u32)(var1 * var2 / 3);
    calc_gas_res = (u32)(var3 * (u32)100 / (u32)data->bme680.par_gh1);

    return calc_gas_res;
}

static int bme680_set_mode(struct bme680_data *data, enum bme680_op_mode mode)
{
    int ret;
    u8 tmp;

    ret = regmap_read(data->regmap, BME680_REG_CTRL_MEAS, &tmp);
    if (ret < 0)
        return ret;

    tmp &= ~BME680_MODE_MASK;
    tmp |= FIELD_PREP(BME680_MODE_MASK, mode);

    return regmap_write(data->regmap, BME680_REG_CTRL_MEAS, tmp);
}

static int bme680_get_mode(struct bme680_data *data, enum bme680_op_mode *mode)
{
    int ret;
    u8 val;

    ret = regmap_read(data->regmap, BME680_REG_CTRL_MEAS, &val);
    if (ret < 0)
        return ret;

    *mode = FIELD_GET(BME680_MODE_MASK, val);

    return 0;
}

static int bme680_wait_for_eoc(struct bme680_data *data)
{
    int ret;
    u8 status;
    int try = 10;

    do {
        ret = regmap_read(data->regmap, BME680_REG_MEAS_STATUS_0, &status);
        if (ret < 0)
            return ret;

        if (!(status & BME680_NEW_DATA_MSK))
            break;

        usleep_range(1000, 2000);
    } while (--try);

    if (!try)
        return -ETIMEDOUT;

    return 0;
}

static int bme680_read_raw_data(struct bme680_data *data)
{
    int ret;
    u8 buf[15];

    ret = bme680_wait_for_eoc(data);
    if (ret < 0)
        return ret;

    ret = regmap_bulk_read(data->regmap, BME680_REG_MEAS_STATUS_0, buf, sizeof(buf));
    if (ret < 0)
        return ret;

    data->pressure_adc = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);
    data->temp_adc = (buf[3] << 12) | (buf[4] << 4) | (buf[5] >> 4);
    data->humid_adc = (buf[6] << 8) | buf[7];
    data->gas_adc = (buf[9] << 8) | buf[10];
    data->gas_range = buf[11] & BME680_GAS_RANGE_MASK;

    return 0;
}

static int bme680_set_oversampling(struct bme680_data *data, u8 osr, u8 reg)
{
    return regmap_update_bits(data->regmap, reg, BME680_OSR_MASK, FIELD_PREP(BME680_OSR_MASK, osr));
}

static int bme680_set_gas_config(struct bme680_data *data)
{
    int ret;
    u8 heatr_conf, heatr_dur, heatr_temp, heatr_val;

    heatr_conf = 0;
    heatr_dur = data->heater_dur;
    heatr_temp = data->heater_temp;

    ret = regmap_write(data->regmap, BME680_REG_GAS_WAIT_0, heatr_dur);
    if (ret < 0)
        return ret;

    heatr_val = data->bme680.res_heat_val;
    ret = regmap_write(data->regmap, BME680_REG_RES_HEAT_0, heatr_val);
    if (ret < 0)
        return ret;

    heatr_conf = BME680_ENABLE_GAS_MEAS_L | BME680_ENABLE_HEATER;
    ret = regmap_write(data->regmap, BME680_REG_CTRL_GAS_1, heatr_conf);
    if (ret < 0)
        return ret;

    return 0;
}

static int bme680_chip_config(struct bme680_data *data)
{
    int ret;
    u8 osrs_p = data->oversampling_press;
    u8 osrs_t = data->oversampling_temp;
    u8 osrs_h = data->oversampling_humid;
    u8 filter = BME680_FILTER_3;

    ret = bme680_set_oversampling(data, osrs_t, BME680_REG_CTRL_MEAS);
    if (ret < 0)
        return ret;

    ret = bme680_set_oversampling(data, osrs_p, BME680_REG_CTRL_MEAS);
    if (ret < 0)
        return ret;

    ret = bme680_set_oversampling(data, osrs_h, BME680_REG_CTRL_HUM);
    if (ret < 0)
        return ret;

    ret = regmap_update_bits(data->regmap, BME680_REG_CONFIG, BME680_FILTER_MASK, FIELD_PREP(BME680_FILTER_MASK, filter));
    if (ret < 0)
        return ret;

    ret = bme680_set_gas_config(data);
    if (ret < 0)
        return ret;

    return 0;
}

static int bme680_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
    struct bme680_data *data = iio_priv(indio_dev);
    int ret;
    enum bme680_op_mode mode;

    mutex_lock(&data->lock);
    lockdep_assert_held(&data->lock); // Lockdep check

    ret = bme680_get_mode(data, &mode);
    if (ret < 0)
        goto out;

    if (mode == BME680_MODE_SLEEP) {
        ret = bme680_set_mode(data, BME680_MODE_FORCED);
        if (ret < 0)
            goto out;
    }

    ret = bme680_read_raw_data(data);
    if (ret < 0)
        goto out;

    switch (mask) {
    case IIO_CHAN_INFO_PROCESSED:
        switch (chan->type) {
        case IIO_TEMP:
            *val = calc_temperature(data, data->temp_adc) / 10;
            *val2 = calc_temperature(data, data->temp_adc) % 10 * 100000;
            ret = IIO_VAL_INT_PLUS_MICRO;
            break;
        case IIO_PRESSURE:
            *val = calc_pressure(data, data->pressure_adc) / 1000;
            *val2 = calc_pressure(data, data->pressure_adc) % 1000 * 1000;
            ret = IIO_VAL_INT_PLUS_MICRO;
            break;
        case IIO_HUMIDITYRELATIVE:
            *val = calc_humidity(data, data->humid_adc) / 1000;
            *val2 = calc_humidity(data, data->humid_adc) % 1000 * 1000;
            ret = IIO_VAL_INT_PLUS_MICRO;
            break;
        case IIO_RESISTANCE:
            if (data->gas_range & BME680_GAS_RANGE_RL_MASK)
                *val = calc_gas_resistance_low(data, data->gas_adc, data->gas_range);
            else
                *val = calc_gas_resistance_high(data, data->gas_adc, data->gas_range);
            ret = IIO_VAL_INT;
            break;
        default:
            ret = -EINVAL;
        }
        break;
    default:
        ret = -EINVAL;
    }

out:
    mutex_unlock(&data->lock);
    return ret;
}

static int bme680_write_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int val, int val2, long mask)
{
    struct bme680_data *data = iio_priv(indio_dev);
    int ret;

    mutex_lock(&data->lock);
    lockdep_assert_held(&data->lock);
    switch (mask) {
    case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
        switch (chan->type) {
        case IIO_TEMP:
            data->oversampling_temp = ilog2(val);
            break;
        case IIO_PRESSURE:
            data->oversampling_press = ilog2(val);
            break;
        case IIO_HUMIDITYRELATIVE:
            data->oversampling_humid = ilog2(val);
            break;
        default:
            ret = -EINVAL;
            goto out;
        }
        ret = bme680_chip_config(data);
        break;
    case IIO_CHAN_INFO_HEATER_TEMP:
        data->heater_temp = val;
        ret = bme680_set_gas_config(data);
        break;
    case IIO_CHAN_INFO_HEATER_DUR:
        data->heater_dur = val;
        ret = bme680_set_gas_config(data);
        break;
    default:
        ret = -EINVAL;
    }

out:
    mutex_unlock(&data->lock);
    return ret;
}

static ssize_t bme680_heater_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct iio_dev *indio_dev = dev_to_iio_dev(dev);
    struct bme680_data *data = iio_priv(indio_dev);

    return sysfs_emit(buf, "%d\n", data->preheat_curr_mA);
}

static ssize_t bme680_heater_current_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct iio_dev *indio_dev = dev_to_iio_dev(dev);
    struct bme680_data *data = iio_priv(indio_dev);
    int val;
    int ret;

    ret = kstrtoint(buf, 10, &val);
    if (ret)
        return ret;

    mutex_lock(&data->lock);
    lockdep_assert_held(&data->lock);
    data->preheat_curr_mA = val;
    ret = bme680_set_gas_config(data);
    mutex_unlock(&data->lock);

    if (ret < 0)
        return ret;

    return count;
}

static DEVICE_ATTR_RW(bme680_heater_current);

static struct attribute *bme680_attrs[] = {
    &dev_attr_bme680_heater_current.attr,
    NULL
};

static const struct attribute_group bme680_attr_group = {
    .attrs = bme680_attrs,
};

static const struct iio_info bme680_info = {
    .read_raw = bme680_read_raw,
    .write_raw = bme680_write_raw,
    .attrs = &bme680_attr_group,
};

static int bme680_trigger_handler(struct iio_poll_func *pf)
{
    struct iio_dev *indio_dev = pf->indio_dev;
    struct bme680_data *data = iio_priv(indio_dev);
    int ret;
    s32 temp;
    u32 press;
    u32 humid;
    u32 gas;

    mutex_lock(&data->lock);
    lockdep_assert_held(&data->lock);
    ret = bme680_read_raw_data(data);
    if (ret < 0)
        goto out;

    temp = calc_temperature(data, data->temp_adc);
    press = calc_pressure(data, data->pressure_adc);
    humid = calc_humidity(data, data->humid_adc);
    if (data->gas_range & BME680_GAS_RANGE_RL_MASK)
        gas = calc_gas_resistance_low(data, data->gas_adc, data->gas_range);
    else
        gas = calc_gas_resistance_high(data, data->gas_adc, data->gas_range);

    iio_push_to_buffers_with_timestamp(indio_dev, &temp, pf->timestamp);

out:
    mutex_unlock(&data->lock);
    iio_trigger_notify_done(indio_dev->trig);

    return IRQ_HANDLED;
}

static int bme680_probe(struct device *dev, struct regmap *regmap, const char *name)
{
    struct iio_dev *indio_dev;
    struct bme680_data *data;
    int ret;
    u8 id;

    indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    data->regmap = regmap;
    data->dev = dev;
    data->oversampling_humid = BME680_OSR_2X;
    data->oversampling_press = BME680_OSR_4X;
    data->oversampling_temp = BME680_OSR_8X;
    data->heater_temp = 320;
    data->heater_dur = 150;
    mutex_init(&data->lock);
    lockdep_register_key(&data->lockdep_map); // Lockdep init
    lockdep_set_class(&data->lock, &data->lockdep_map);

    indio_dev->name = name ? name : "bme680";
    indio_dev->info = &bme680_info;
    indio_dev->channels = bme680_channels;
    indio_dev->num_channels = ARRAY_SIZE(bme680_channels);
    indio_dev->modes = INDIO_DIRECT_MODE;

    ret = regmap_write(regmap, BME680_REG_SOFT_RESET, BME680_CMD_SOFTRESET);
    if (ret < 0)
        return ret;

    usleep_range(5000, 6000);

    ret = regmap_read(regmap, BME680_REG_CHIP_ID, &id);
    if (ret < 0)
        return ret;

    if (id != BME680_CHIP_ID) {
        dev_err(dev, "Wrong chip id 0x%x\n", id);
        return -ENODEV;
    }

    ret = bme680_read_calib(data, &data->bme680);
    if (ret < 0)
        return ret;

    ret = bme680_chip_config(data);
    if (ret < 0)
        return ret;

    ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(bme680_supply_names), data->supplies);
    if (ret)
        return dev_err_probe(dev, ret, "failed to enable regulators\n");

    ret = bme680_set_mode(data, BME680_MODE_SLEEP);
    if (ret < 0)
        return ret;

    ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL, bme680_trigger_handler, NULL);
    if (ret < 0)
        return ret;

    data->trig = devm_iio_trigger_alloc(dev, "%s-dev%d", indio_dev->name, iio_get_id(indio_dev));
    if (!data->trig)
        return -ENOMEM;

    data->trig->ops = &bme680_trigger_ops;
    iio_trigger_set_drvdata(data->trig, indio_dev);
    ret = devm_iio_trigger_register(dev, data->trig);
    if (ret)
        return ret;

    ret = pm_runtime_set_active(dev);
    if (ret)
        return ret;

    pm_runtime_enable(dev);
    pm_runtime_set_autosuspend_delay(dev, 1000);
    pm_runtime_use_autosuspend(dev);

    return devm_iio_device_register(dev, indio_dev);
}

static const struct dev_pm_ops bme680_dev_pm_ops = {
    SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
    RUNTIME_PM_OPS(bme680_runtime_suspend, bme680_runtime_resume, NULL)
};

MODULE_AUTHOR("Nguyen Nhan");
MODULE_DESCRIPTION("Bosch BME680 sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_BME680);