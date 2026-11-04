#ifndef SMS_APP_H
#define SMS_APP_H

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>

#define SMS_DEVICE       "/dev/sms_sensor"
#define SMS_LOG_FILE     "/tmp/sms.log"
#define SMS_QUEUE_ALERT  "/sms_alert_queue"
#define SMS_QUEUE_LOG    "/sms_log_queue"

#define SMS_BUFFER_SIZE  64
#define SMS_AVG_WINDOW   10
#define SMS_DEFAULT_INTERVAL_MS 500

typedef struct {
    uint64_t timestamp_ms;
    double temperature;
    double humidity;
} sensor_data_t;

typedef struct {
    sensor_data_t items[SMS_BUFFER_SIZE];
    int head;
    int tail;
    int count;
    bool stopped;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} sensor_buffer_t;

typedef struct {
    int interval_ms;
    int duration_seconds;
    bool debug;
    bool verbose;
} sms_config_t;

extern volatile sig_atomic_t sms_running;
extern sensor_buffer_t sms_buffer;
extern sms_config_t sms_config;

void *sensor_thread(void *arg);
void *processor_thread(void *arg);
void *alert_thread(void *arg);
void *logger_thread(void *arg);

int buffer_push(const sensor_data_t *data);
int buffer_pop(sensor_data_t *data);

#endif

int sms_queues_init(void);
void sms_queues_cleanup(void);
void sms_queues_stop_workers(void);
void sms_queues_stop_logger(void);
