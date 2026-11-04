#define _POSIX_C_SOURCE 200809L

#include "sms_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <mqueue.h>
#include <stdarg.h>

#include "../driver/sms_sensor.h"

#define SMS_MSG_SIZE 256

typedef struct {
    char text[SMS_MSG_SIZE];
} sms_message_t;

static mqd_t alert_queue = (mqd_t)-1;
static mqd_t log_queue = (mqd_t)-1;

static void send_message(mqd_t queue, const char *text)
{
    if (queue == (mqd_t)-1 || text == NULL)
        return;

    if (mq_send(queue, text, strlen(text) + 1, 0) == -1 &&
        sms_config.debug) {
        perror("mq_send");
    }
}

static void log_message(const char *format, ...)
{
    char text[SMS_MSG_SIZE];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    send_message(log_queue, text);
}

int buffer_push(const sensor_data_t *data)
{
    if (data == NULL)
        return -1;

    pthread_mutex_lock(&sms_buffer.mutex);

    while (sms_buffer.count == SMS_BUFFER_SIZE &&
           !sms_buffer.stopped) {
        pthread_cond_wait(&sms_buffer.not_full, &sms_buffer.mutex);
    }

    if (sms_buffer.stopped) {
        pthread_mutex_unlock(&sms_buffer.mutex);
        return -1;
    }

    sms_buffer.items[sms_buffer.tail] = *data;
    sms_buffer.tail = (sms_buffer.tail + 1) % SMS_BUFFER_SIZE;
    sms_buffer.count++;

    pthread_cond_signal(&sms_buffer.not_empty);
    pthread_mutex_unlock(&sms_buffer.mutex);

    return 0;
}

int buffer_pop(sensor_data_t *data)
{
    if (data == NULL)
        return -1;

    pthread_mutex_lock(&sms_buffer.mutex);

    while (sms_buffer.count == 0 &&
           !sms_buffer.stopped) {
        pthread_cond_wait(&sms_buffer.not_empty, &sms_buffer.mutex);
    }

    if (sms_buffer.count == 0 && sms_buffer.stopped) {
        pthread_mutex_unlock(&sms_buffer.mutex);
        return -1;
    }

    *data = sms_buffer.items[sms_buffer.head];
    sms_buffer.head = (sms_buffer.head + 1) % SMS_BUFFER_SIZE;
    sms_buffer.count--;

    pthread_cond_signal(&sms_buffer.not_full);
    pthread_mutex_unlock(&sms_buffer.mutex);

    return 0;
}

static int create_queue(const char *name, mqd_t *queue)
{
    struct mq_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = SMS_MSG_SIZE;

    *queue = mq_open(name, O_CREAT | O_RDWR, 0600, &attr);
    if (*queue == (mqd_t)-1) {
        perror("mq_open");
        return -1;
    }

    return 0;
}

int sms_queues_init(void)
{
    if (create_queue(SMS_QUEUE_ALERT, &alert_queue) != 0)
        return -1;

    if (create_queue(SMS_QUEUE_LOG, &log_queue) != 0) {
        mq_close(alert_queue);
        mq_unlink(SMS_QUEUE_ALERT);
        alert_queue = (mqd_t)-1;
        return -1;
    }

    return 0;
}

void sms_queues_cleanup(void)
{
    if (alert_queue != (mqd_t)-1) {
        mq_close(alert_queue);
        mq_unlink(SMS_QUEUE_ALERT);
        alert_queue = (mqd_t)-1;
    }

    if (log_queue != (mqd_t)-1) {
        mq_close(log_queue);
        mq_unlink(SMS_QUEUE_LOG);
        log_queue = (mqd_t)-1;
    }
}

void *sensor_thread(void *arg)
{
    (void)arg;

    int interval = sms_config.interval_ms;

    int control_fd = open(SMS_DEVICE, O_RDWR);
    if (control_fd == -1) {
        perror("open " SMS_DEVICE);
        sms_running = 0;
        return NULL;
    }

    if (ioctl(control_fd, SMS_SET_INTERVAL, &interval) == -1)
        perror("ioctl SMS_SET_INTERVAL");

    close(control_fd);

    log_message("Sensor thread started; interval=%d ms", interval);

    struct timespec delay = {
        .tv_sec = interval / 1000,
        .tv_nsec = (long)(interval % 1000) * 1000000L
    };

    while (sms_running) {
        int fd = open(SMS_DEVICE, O_RDONLY);
        if (fd == -1) {
            perror("open sensor");
            break;
        }

        char raw[128] = {0};
        ssize_t n = read(fd, raw, sizeof(raw) - 1);
        close(fd);

        if (n > 0) {
            unsigned long long timestamp;
            int temperature;
            int humidity;

            raw[n] = '\0';

            if (sscanf(raw, "%llu,%d,%d",
                       &timestamp, &temperature, &humidity) == 3) {
                sensor_data_t sample = {
                    .timestamp_ms = (uint64_t)timestamp,
                    .temperature = temperature,
                    .humidity = humidity
                };

                if (buffer_push(&sample) == 0) {
                    if (sms_config.verbose) {
                        printf("[SENSOR] t=%llu temp=%.1f humidity=%.1f\n",
                               timestamp, sample.temperature,
                               sample.humidity);
                    }

                    log_message("SENSOR,%llu,%.1f,%.1f",
                                timestamp, sample.temperature,
                                sample.humidity);
                }
            } else {
                log_message("ERROR,invalid sensor data");
            }
        } else if (n < 0 && errno != EINTR) {
            perror("read sensor");
            break;
        }

        nanosleep(&delay, NULL);
    }

    log_message("Sensor thread stopped");
    return NULL;
}
void *processor_thread(void *arg)
{
    (void)arg;

    sensor_data_t sample;
    double temperatures[SMS_AVG_WINDOW] = {0};
    double humidities[SMS_AVG_WINDOW] = {0};
    int used = 0;
    int next = 0;
    double previous_temperature = -1.0;

    while (buffer_pop(&sample) == 0) {
        temperatures[next] = sample.temperature;
        humidities[next] = sample.humidity;
        next = (next + 1) % SMS_AVG_WINDOW;
        if (used < SMS_AVG_WINDOW)
            used++;

        double sum_t = 0.0;
        double sum_h = 0.0;

        for (int i = 0; i < used; i++) {
            sum_t += temperatures[i];
            sum_h += humidities[i];
        }

        double avg_t = sum_t / used;
        double avg_h = sum_h / used;

        if (sms_config.verbose) {
            printf("[PROCESSOR] avg_temp=%.2f avg_humidity=%.2f\n",
                   avg_t, avg_h);
        }

        log_message("AVERAGE,%.2f,%.2f", avg_t, avg_h);

        if (sample.temperature > 40.0) {
            char msg[SMS_MSG_SIZE];
            snprintf(msg, sizeof(msg),
                     "ALERT: High temperature %.1f C",
                     sample.temperature);
            send_message(alert_queue, msg);
        }

        if (sample.humidity > 80.0) {
            char msg[SMS_MSG_SIZE];
            snprintf(msg, sizeof(msg),
                     "ALERT: High humidity %.1f%%",
                     sample.humidity);
            send_message(alert_queue, msg);
        }

        if (previous_temperature >= 0.0 &&
            abs((int)(sample.temperature - previous_temperature)) > 5) {
            char msg[SMS_MSG_SIZE];
            snprintf(msg, sizeof(msg),
                     "ALERT: Temperature changed from %.1f to %.1f C",
                     previous_temperature, sample.temperature);
            send_message(alert_queue, msg);
        }

        previous_temperature = sample.temperature;
    }

    log_message("Processor thread stopped");
    return NULL;
}

void *alert_thread(void *arg)
{
    (void)arg;

    char msg[SMS_MSG_SIZE];

    while (1) {
        ssize_t n = mq_receive(alert_queue, msg, sizeof(msg), NULL);

        if (n >= 0) {
            msg[n < SMS_MSG_SIZE ? n : SMS_MSG_SIZE - 1] = '\0';

            if (strcmp(msg, "__SMS_STOP__") == 0)
                break;

            printf("[ALERT] %s\n", msg);
            log_message("%s", msg);
        } else if (errno == EINTR) {
            continue;
        } else {
            perror("mq_receive alert");
            break;
        }
    }

    return NULL;
}

void *logger_thread(void *arg)
{
    (void)arg;

    FILE *fp = fopen(SMS_LOG_FILE, "a");
    if (fp == NULL) {
        perror("fopen " SMS_LOG_FILE);
        return NULL;
    }

    char msg[SMS_MSG_SIZE];
    time_t last_flush = time(NULL);
    const long max_log_size = 1024L * 1024L;

    while (1) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 1;

        ssize_t n = mq_timedreceive(log_queue, msg, sizeof(msg),
                                    NULL, &timeout);

        if (n >= 0) {
            msg[n < SMS_MSG_SIZE ? n : SMS_MSG_SIZE - 1] = '\0';

            if (strcmp(msg, "__SMS_STOP__") == 0)
                break;

            if (fprintf(fp, "%ld,%s\n", (long)time(NULL), msg) < 0) {
                perror("write log");
            }

            long position = ftell(fp);

            if (position >= max_log_size) {
                if (fflush(fp) == 0) {
                    if (fclose(fp) != 0) {
                        perror("close log before rotation");
                        return NULL;
                    }
                    fp = NULL;

                    (void)unlink(SMS_LOG_FILE ".1");

                    if (rename(SMS_LOG_FILE, SMS_LOG_FILE ".1") == -1) {
                        perror("rotate log");
                        fp = fopen(SMS_LOG_FILE, "a");
                    } else {
                        fp = fopen(SMS_LOG_FILE, "a");
                    }

                    if (fp == NULL) {
                        perror("reopen log after rotation");
                        return NULL;
                    }

                    last_flush = time(NULL);
                }
            }
        } else if (errno != ETIMEDOUT && errno != EINTR) {
            perror("mq_timedreceive log");
            break;
        }

        time_t now = time(NULL);
        if (now - last_flush >= 5) {
            if (fflush(fp) != 0)
                perror("fflush log");

            last_flush = now;
        }
    }

    if (fflush(fp) != 0)
        perror("final fflush log");

    fclose(fp);
    return NULL;
}

void sms_queues_stop_workers(void)
{
    const char *stop_msg = "__SMS_STOP__";

    if (alert_queue != (mqd_t)-1) {
        if (mq_send(alert_queue, stop_msg, strlen(stop_msg) + 1, 0) == -1)
            perror("mq_send stop alert");
    }
}

void sms_queues_stop_logger(void)
{
    const char *stop_msg = "__SMS_STOP__";

    if (log_queue != (mqd_t)-1) {
        if (mq_send(log_queue, stop_msg, strlen(stop_msg) + 1, 0) == -1)
            perror("mq_send stop logger");
    }
}
