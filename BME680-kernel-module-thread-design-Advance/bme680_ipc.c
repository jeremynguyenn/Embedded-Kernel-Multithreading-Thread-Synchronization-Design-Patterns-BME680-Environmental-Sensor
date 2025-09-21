// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/uaccess.h>
#include <linux/ipc.h>
#include "bme680.h"
#include "timer.h"
#include <linux/lockdep.h> // Thêm lockdep

struct bme680_ipc_data {
    struct sock *nl_sk;
    struct bme680_data *data;
    int sysv_msgid;
    struct mutex lock;
    timer_t *timer;
    lockdep_map lockdep_map; // Lockdep
};

static struct bme680_ipc_data *ipc_data;

struct bme680_sysv_msg {
    long mtype;
    char mtext[64];
};

void bme680_send_netlink_alert(struct bme680_data *data, const char *msg) {
    struct sk_buff *skb;
    struct nlmsghdr *nlh;
    int res;

    skb = nlmsg_new(strlen(msg) + 1, GFP_KERNEL);
    if (!skb) return;

    nlh = nlmsg_put(skb, 0, 0, NLMSG_DONE, strlen(msg) + 1, 0);
    if (!nlh) {
        kfree_skb(skb);
        return;
    }

    strcpy(nlmsg_data(nlh), msg);
    mutex_lock(&ipc_data->lock);
    lockdep_assert_held(&ipc_data->lock); // Lockdep check
    res = nlmsg_multicast(ipc_data->nl_sk, skb, 0, 1, GFP_KERNEL);
    mutex_unlock(&ipc_data->lock);
    if (res < 0) kfree_skb(skb);
}

void bme680_send_sysv_alert(struct bme680_data *data, const char *msg) {
    struct bme680_sysv_msg sysv_msg;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 5; // Timeout for msgsnd
    sysv_msg.mtype = 1;
    strncpy(sysv_msg.mtext, msg, sizeof(sysv_msg.mtext) - 1);
    sysv_msg.mtext[sizeof(sysv_msg.mtext) - 1] = '\0';

    mutex_lock(&ipc_data->lock);
    lockdep_assert_held(&ipc_data->lock);
    int retries = 3;
    while (retries--) {
        if (msgsnd(ipc_data->sysv_msgid, &sysv_msg, sizeof(sysv_msg.mtext), IPC_NOWAIT) == 0) {
            mutex_unlock(&ipc_data->lock);
            return;
        }
        if (errno != EAGAIN) {
            pr_err("bme680: Failed to send System V message: %s\n", strerror(errno));
            mutex_unlock(&ipc_data->lock);
            return;
        }
        // Backoff with timeout
        if (clock_nanosleep(CLOCK_MONOTONIC, 0, & (struct timespec){.tv_nsec = 100000000}, NULL) == ETIMEDOUT) {
            pr_err("bme680: Timed out sending System V message");
            mutex_unlock(&ipc_data->lock);
            return;
        }
    }
    mutex_unlock(&ipc_data->lock);
}

static void check_threshold_task(void *arg) {
    struct bme680_data *data = (struct bme680_data *)arg;
    if (data->temp_adc > THRESHOLD_TEMP) {
        bme680_send_netlink_alert(data, "High temperature alert");
        iio_push_event(data->indio_dev, IIO_EVENT_CODE(IIO_TEMP, 0, IIO_EV_TYPE_THRESH, IIO_EV_DIR_RISING), ktime_get_ns());
    }
    if (data->gas_adc > THRESHOLD_GAS) {
        bme680_send_sysv_alert(data, "High gas alert");
        iio_push_event(data->indio_dev, IIO_EVENT_CODE(IIO_RESISTANCE, 0, IIO_EV_TYPE_THRESH, IIO_EV_DIR_RISING), ktime_get_ns());
    }
    // Hoàn thiện truncated: Thêm check pressure và humidity
    if (data->pressure_adc > THRESHOLD_PRESSURE) {
        bme680_send_netlink_alert(data, "High pressure alert");
    }
    if (data->humid_adc > THRESHOLD_HUMID) {
        bme680_send_sysv_alert(data, "High humidity alert");
    }
}

int bme680_ipc_init(struct bme680_data *data) {
    struct netlink_kernel_cfg cfg = { .input = NULL };
    key_t sysv_key = 1234;

    ipc_data = kzalloc(sizeof(*ipc_data), GFP_KERNEL);
    if (!ipc_data) return -ENOMEM;

    mutex_init(&ipc_data->lock);
    lockdep_register_key(&ipc_data->lockdep_map);
    lockdep_set_class(&ipc_data->lock, &ipc_data->lockdep_map);
    ipc_data->data = data;
    ipc_data->nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
    if (!ipc_data->nl_sk) {
        mutex_destroy(&ipc_data->lock);
        kfree(ipc_data);
        return -ENOMEM;
    }

    ipc_data->sysv_msgid = msgget(sysv_key, IPC_CREAT | 0666);
    if (ipc_data->sysv_msgid < 0) {
        pr_err("bme680: Failed to create System V message queue\n");
        netlink_kernel_release(ipc_data->nl_sk);
        mutex_destroy(&ipc_data->lock);
        kfree(ipc_data);
        return -ENOMEM;
    }

    timer_init(&ipc_data->timer, 1000, check_threshold_task, data); // Check threshold every 1s
    return 0;
}

void bme680_ipc_cleanup(struct bme680_data *data) {
    if (ipc_data) {
        mutex_lock(&ipc_data->lock);
        timer_destroy(ipc_data->timer);
        if (ipc_data->nl_sk) {
            netlink_kernel_release(ipc_data->nl_sk);
        }
        if (ipc_data->sysv_msgid >= 0) {
            msgctl(ipc_data->sysv_msgid, IPC_RMID, NULL);
        }
        mutex_unlock(&ipc_data->lock);
        mutex_destroy(&ipc_data->lock);
        kfree(ipc_data);
    }
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nguyen Nhan");
MODULE_DESCRIPTION("BME680 IPC Driver");
MODULE_VERSION("3.2.1");