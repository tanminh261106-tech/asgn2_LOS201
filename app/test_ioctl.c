#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../driver/sms_sensor.h"

int main(void)
{
    int fd = open("/dev/sms_sensor", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    int interval = 500;

    if (ioctl(fd, SMS_SET_INTERVAL, &interval) < 0) {
        perror("ioctl SMS_SET_INTERVAL");
        close(fd);
        return 1;
    }

    printf("SMS_SET_INTERVAL: PASS (interval=%d ms)\n", interval);

    close(fd);
    return 0;
}
