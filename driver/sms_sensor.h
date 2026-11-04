#ifndef SMS_SENSOR_H
#define SMS_SENSOR_H

#include <linux/ioctl.h>

#define SMS_IOC_MAGIC 'S'

#define SMS_SET_INTERVAL _IOW(SMS_IOC_MAGIC, 1, int)

#endif
