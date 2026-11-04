#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/random.h>
#include <linux/timekeeping.h>
#include <linux/jiffies.h>
#include <linux/ioctl.h>

#include "sms_sensor.h"

#define DRIVER_NAME "sms_sensor"
#define DEFAULT_INTERVAL_MS 100
#define MIN_INTERVAL_MS 10
#define MAX_INTERVAL_MS 10000

static DEFINE_MUTEX(sensor_mutex);

static int sample_interval_ms = DEFAULT_INTERVAL_MS;
static unsigned int read_count;
static int last_temperature = 25;
static int last_humidity = 50;
static unsigned long start_jiffies;

/* Read one simulated sensor sample as CSV */
static ssize_t sms_sensor_read(struct file *file,
                               char __user *user_buf,
                               size_t count,
                               loff_t *offset)
{
    char buffer[80];
    int len;
    u32 random_value;
    u64 timestamp_ms;

    if (*offset > 0)
        return 0;

    timestamp_ms = ktime_to_ms(ktime_get());

    get_random_bytes(&random_value, sizeof(random_value));
    mutex_lock(&sensor_mutex);
    last_temperature = 15 + (random_value % 31);

    get_random_bytes(&random_value, sizeof(random_value));
    last_humidity = 30 + (random_value % 61);

    read_count++;

    len = scnprintf(buffer, sizeof(buffer), "%llu,%d,%d\n",
                    (unsigned long long)timestamp_ms,
                    last_temperature,
                    last_humidity);
    mutex_unlock(&sensor_mutex);

    if (count < len)
        return -EINVAL;

    if (copy_to_user(user_buf, buffer, len))
        return -EFAULT;

    *offset += len;
    return len;
}

/* Configure the sensor sampling interval */
static long sms_sensor_ioctl(struct file *file,
                             unsigned int cmd,
                             unsigned long arg)
{
    int interval;

    if (cmd != SMS_SET_INTERVAL)
        return -ENOTTY;

    if (copy_from_user(&interval, (int __user *)arg, sizeof(interval)))
        return -EFAULT;

    if (interval < MIN_INTERVAL_MS || interval > MAX_INTERVAL_MS)
        return -EINVAL;

    mutex_lock(&sensor_mutex);
    sample_interval_ms = interval;
    mutex_unlock(&sensor_mutex);

    pr_info("sms_sensor: interval set to %d ms\n", interval);
    return 0;
}

static const struct file_operations sms_sensor_fops = {
    .owner = THIS_MODULE,
    .read = sms_sensor_read,
    .unlocked_ioctl = sms_sensor_ioctl,
    .llseek = no_llseek,
};

static struct miscdevice sms_sensor_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DRIVER_NAME,
    .fops = &sms_sensor_fops,
    .mode = 0666,
};

/* Report driver statistics through /proc/sms_stats */
static int sms_stats_show(struct seq_file *seq, void *v)
{
    unsigned int reads;
    int temperature;
    int humidity;
    int interval;
    unsigned long uptime_seconds;

    mutex_lock(&sensor_mutex);
    reads = read_count;
    temperature = last_temperature;
    humidity = last_humidity;
    interval = sample_interval_ms;
    uptime_seconds = (jiffies - start_jiffies) / HZ;
    mutex_unlock(&sensor_mutex);

    seq_printf(seq, "read_count: %u\n", reads);
    seq_printf(seq, "last_temperature: %d\n", temperature);
    seq_printf(seq, "last_humidity: %d\n", humidity);
    seq_printf(seq, "interval_ms: %d\n", interval);
    seq_printf(seq, "uptime_seconds: %lu\n", uptime_seconds);

    return 0;
}

static int sms_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, sms_stats_show, NULL);
}

static const struct proc_ops sms_stats_ops = {
    .proc_open = sms_stats_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static int __init sms_sensor_init(void)
{
    int ret;

    start_jiffies = jiffies;

    ret = misc_register(&sms_sensor_device);
    if (ret) {
        pr_err("sms_sensor: failed to register device: %d\n", ret);
        return ret;
    }

    if (!proc_create("sms_stats", 0444, NULL, &sms_stats_ops)) {
        misc_deregister(&sms_sensor_device);
        pr_err("sms_sensor: failed to create /proc/sms_stats\n");
        return -ENOMEM;
    }

    pr_info("sms_sensor: initialized, interval=%d ms\n",
            sample_interval_ms);
    return 0;
}

static void __exit sms_sensor_exit(void)
{
    remove_proc_entry("sms_stats", NULL);
    misc_deregister(&sms_sensor_device);
    pr_info("sms_sensor: removed\n");
}

module_init(sms_sensor_init);
module_exit(sms_sensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Systems Student");
MODULE_DESCRIPTION("Simulated temperature and humidity sensor driver");
MODULE_VERSION("1.0");
