#ifndef _BME680_H_
#define _BME680_H_

#include <linux/bitfield.h>
#include <linux/types.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/hwmon.h>
#include <linux/netlink.h>
#include <linux/rcupdate.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/completion.h>
#include <linux/shmem_fs.h>
#include <linux/msg.h>
#include <linux/timer.h>

typedef enum {
    BME680_MODE_SLEEP = 0,
    BME680_MODE_FORCED = 1,
    BME680_MODE_CONTINUOUS = 2,
} bme680_mode_t;

typedef enum {
    BME680_OVERSAMPLING_SKIP = 0,
    BME680_OVERSAMPLING_X1 = 1,
    BME680_OVERSAMPLING_X2 = 2,
    BME680_OVERSAMPLING_X4 = 3,
    BME680_OVERSAMPLING_X8 = 4,
    BME680_OVERSAMPLING_X16 = 5,
} bme680_oversampling_t;

typedef enum {
    BME680_FILTER_OFF = 0,
    BME680_FILTER_COEFF_1 = 1,
    BME680_FILTER_COEFF_3 = 2,
    BME680_FILTER_COEFF_7 = 3,
    BME680_FILTER_COEFF_15 = 4,
    BME680_FILTER_COEFF_31 = 5,
    BME680_FILTER_COEFF_63 = 6,
    BME680_FILTER_COEFF_127 = 7,
} bme680_filter_t;

/* Register definitions */
#define BME680_REG_CHIP_ID 0xD0
#define BME680_CHIP_ID_VAL 0x61
#define BME680_REG_SOFT_RESET 0xE0
#define BME680_CMD_SOFTRESET 0xB6
#define BME680_REG_STATUS 0x73
#define BME680_REG_CTRL_HUM 0x72
#define BME680_OSRS_HUM_MSK GENMASK(2, 0)
#define BME680_REG_CTRL_MEAS 0x74
#define BME680_OSRS_TEMP_MSK GENMASK(7, 5)
#define BME680_OSRS_PRESS_MSK GENMASK(4, 2)
#define BME680_MODE_MSK GENMASK(1, 0)
#define BME680_REG_CONFIG 0x75
#define BME680_FILTER_MSK GENMASK(4, 2)
#define BME680_REG_CTRL_GAS_1 0x71
#define BME680_RUN_GAS_MSK BIT(4)
#define BME680_NB_CONV_MSK GENMASK(3, 0)
#define BME680_REG_GAS_WAIT_0 0x64
#define BME680_REG_RES_HEAT_0 0x5A
#define BME680_REG_IDAC_HEAT_0 0x50
#define BME680_REG_TEMP_MSB 0x22
#define BME680_REG_TEMP_LSB 0x23
#define BME680_REG_TEMP_XLSB 0x24
#define BME680_REG_PRESS_MSB 0x1F
#define BME680_REG_PRESS_LSB 0x20
#define BME680_REG_PRESS_XLSB 0x21
#define BME680_REG_HUM_MSB 0x25
#define BME680_REG_HUM_LSB 0x26
#define BME680_REG_GAS_R_MSB 0x2A
#define BME680_REG_GAS_R_LSB 0x2B
#define BME680_GAS_STAB_BIT BIT(4)
#define BME680_GAS_RANGE_MASK GENMASK(3, 0)
#define BME680_REG_MEAS_STAT_0 0x1D
#define BME680_NEW_DATA_MSK BIT(7)
#define BME680_GAS_MEAS_BIT BIT(6)
#define BME680_MEAS_BIT BIT(5)
#define BME680_REG_COEFF1 0x89
#define BME680_LEN_COEFF1 25
#define BME680_REG_COEFF2 0x8A
#define BME680_LEN_COEFF2 16
#define BME680_REG_COEFF3 0xE1
#define BME680_LEN_COEFF3 8
#define BME680_LEN_COEFF_ALL (BME680_LEN_COEFF1 + BME680_LEN_COEFF2 + BME680_LEN_COEFF3)
#define BME680_MAX_OVERFLOW_VAL 0x40000000
#define BME680_HUM_REG_SHIFT_VAL 4
#define BME680_ADC_GAS_RES GENMASK(9, 0)
#define BME680_AMB_TEMP 25
#define BME680_TEMP_NUM_BYTES 3
#define BME680_PRESS_NUM_BYTES 3
#define BME680_HUMID_NUM_BYTES 2
#define BME680_GAS_NUM_BYTES 2
#define BME680_MEAS_TRIM_MSK GENMASK(24, 4)
#define BME680_STARTUP_TIME_US 2000
#define BME680_NUM_CHANNELS 4
#define BME680_NUM_BULK_READ_REGS 15
#define BME680_REG_CTRL_GAS_0 0x70
#define BME680_REG_VARIANT_ID 0xF0
#define BME680_SOFT_RESET_VAL 0xB6
#define BME680_STATUS_GAS_VALID_MSK 0x20
#define BME680_GAS_MEAS_MSK 0x30
#define BME680_NBCONV_MSK 0x0F
#define BME680_RUN_GAS_POS 4
#define BME680_FILTER_POS 2
#define BME680_OST_MSK 0xE0
#define BME680_OST_POS 5
#define BME680_OSP_MSK 0x1C
#define BME680_OSP_POS 2
#define BME680_OSH_MSK 0x07
#define BME680_GAS_SENSOR_NB_CONV_MIN 0
#define BME680_GAS_SENSOR_NB_CONV_MAX 10
#define BME680_GAS_WAIT_SHARED_MSK 0x3F
#define BME680_CALIB1_RANGE_1 0xDF
#define BME680_CALIB1_RANGE_2 0x8A
#define BME680_CALIB2_RANGE_1 0xE1
#define BME680_CALIB2_RANGE_2 0xF0


struct bme680_calib {
    uint16_t par_t1;
    int16_t par_t2;
    int8_t par_t3;
    uint16_t par_p1;
    int16_t par_p2;
    int8_t par_p3;
    int16_t par_p4;
    int16_t par_p5;
    int8_t par_p6;
    int8_t par_p7;
    int16_t par_p8;
    int16_t par_p9;
    uint8_t par_p10;
    uint16_t par_h1;
    uint16_t par_h2;
    int8_t par_h3;
    int8_t par_h4;
    int8_t par_h5;
    uint8_t par_h6;
    int8_t par_h7;
    int8_t par_gh1;
    int16_t par_gh2;
    int8_t par_gh3;
    int8_t res_heat_val;
    uint8_t res_heat_range;
    int8_t range_sw_err;
};

struct bme680_field_data {
    int32_t temperature;
    uint32_t pressure;
    uint32_t humidity;
    uint32_t gas_resistance;
    uint8_t gas_range;
    bool heat_stable;
};

struct bme680_fifo_data {
    int64_t timestamp;
    int32_t temperature;
    uint32_t pressure;
    uint32_t humidity;
    uint32_t gas_resistance;
    uint32_t iaq_index;
};

struct bme680_gas_config {
    uint16_t heater_temp;
    uint16_t heater_dur;
    uint8_t preheat_curr_ma;
};

struct bme680_data {
    struct iio_dev *indio_dev;
    struct regmap *regmap;
    struct bme680_calib calib;
    struct mutex lock;
    spinlock_t reg_lock;
    rwlock_t calib_lock;
    wait_queue_head_t wait_data;
    DECLARE_KFIFO(data_fifo, struct bme680_fifo_data, 64);
    struct semaphore fifo_sem;
    struct task_struct *poll_thread;
    atomic_t read_count;
    atomic_t error_count;
    unsigned long start_time;
    int32_t t_fine;
    uint8_t oversampling_temp;
    uint8_t oversampling_press;
    uint8_t oversampling_humid;
    uint8_t filter_coeff;
    uint16_t heater_dur;
    uint16_t heater_temp;
    uint8_t preheat_curr_ma;
    bool gas_enable;
    bme680_mode_t mode;
    struct device *dev;
    struct cdev cdev;
    struct dentry *debugfs_dir;
    void *shm_buffer;
    size_t shm_size;
    uint8_t chip_id;
    uint8_t heater_profile_len;
    uint16_t heater_dur_profile[10];
    uint16_t heater_temp_profile[10];
    uint8_t heater_res[10];
    uint8_t heater_idac[10];
    struct bme680_gas_config gas_config;
    struct bme680_fifo_data fifo_data;
    struct device *hwmon_dev;
    uint32_t gas_threshold;
	struct iio_trigger *trig;
    struct kthread_worker poll_worker;
    struct kthread_work poll_work;
    struct completion data_ready;
    struct netlink_capability *netlink;
    struct hwmon_device *hwmon;
    struct file *ipc_shm;
    int ipc_msgid;
    struct timer_list threshold_timer;
    u32 threshold_temp;
    u32 threshold_press;
    u32 threshold_hum;
    uint8_t heater_temp; /* Thêm trường này, trùng tên nhưng cần đảm bảo không xung đột */
    uint8_t variant_id;
};

#define BME680_IOC_MAGIC 'B'
#define BME680_IOC_SET_GAS_CONFIG _IOW(BME680_IOC_MAGIC, 1, struct bme680_gas_config)
#define BME680_IOC_READ_FIFO _IOR(BME680_IOC_MAGIC, 2, struct bme680_fifo_data)
#define BME680_IOC_GET_SHM _IOR(BME680_IOC_MAGIC, 3, unsigned long)
#define BME680_IOC_SET_HEATER_TEMP _IOW(BME680_IOC_MAGIC, 1, u8)
#define BME680_IOC_SET_HEATER_DUR _IOW(BME680_IOC_MAGIC, 2, u16)
#define BME680_IOC_GET_IAQ _IOR(BME680_IOC_MAGIC, 4, u32)
#define BME680_IOC_SET_THRESHOLD_TEMP _IOW(BME680_IOC_MAGIC, 5, u32)
#define BME680_IOC_SET_THRESHOLD_PRESS _IOW(BME680_IOC_MAGIC, 6, u32)
#define BME680_IOC_SET_THRESHOLD_HUM _IOW(BME680_IOC_MAGIC, 7, u32)
#define BME680_IOC_SET_THRESHOLD_GAS _IOW(BME680_IOC_MAGIC, 8, u32)


extern const struct iio_chan_spec bme680_channels[];
extern const struct regmap_config bme680_regmap_config;
extern struct mutex bme680_i2c_lock;
int bme680_core_probe(struct device *dev, struct regmap *regmap, const char *name, void *bus_data);
int bme680_core_remove(struct device *dev);
int bme680_read_raw(struct iio_dev *indio_dev, struct iio_chan_spec const *chan, int *val, int *val2, long mask);
int bme680_read_data(struct bme680_data *data, struct bme680_fifo_data *fdata);
int bme680_read_calib(struct bme680_data *data);
int bme680_compensate_temperature(const struct bme680_calib *calib, int32_t adc_temp, int32_t *temp, int32_t *t_fine);
int bme680_compensate_pressure(const struct bme680_calib *calib, int32_t adc_press, int32_t t_fine, uint32_t *press);
int bme680_compensate_humidity(const struct bme680_calib *calib, int32_t adc_hum, int32_t t_fine, uint32_t *hum);
int bme680_compensate_gas_resistance(const struct bme680_calib *calib, uint16_t adc_gas_res, uint8_t gas_range, uint32_t *gas_res);
int bme680_calculate_iaq(struct bme680_data *data, uint32_t *iaq);
void bme680_send_netlink_alert(struct bme680_data *data, const char *msg);
void bme680_check_threshold(struct bme680_data *data);
int bme680_ipc_init(struct bme680_data *data);
void bme680_ipc_cleanup(struct bme680_data *data);
int bme680_core_suspend(struct bme680_data *data);
int bme680_core_resume(struct bme680_data *data);

#endif /* _BME680_H_ */