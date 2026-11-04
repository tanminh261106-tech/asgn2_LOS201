#define _POSIX_C_SOURCE 200809L

#include "sms_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>

volatile sig_atomic_t sms_running = 1;

sensor_buffer_t sms_buffer = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
    .not_full = PTHREAD_COND_INITIALIZER
};

sms_config_t sms_config = {
    .interval_ms = SMS_DEFAULT_INTERVAL_MS,
    .duration_seconds = 0,
    .debug = false,
    .verbose = false
};

static void handle_signal(int signo)
{
    (void)signo;
    sms_running = 0;
}

static void print_usage(const char *program)
{
    printf("Usage: %s [-i interval_ms] [-t duration_seconds] [-d] [-v] [-h]\n",
           program);
    printf("  -i N  Sensor interval in milliseconds (10..10000, default 500)\n");
    printf("  -t N  Runtime in seconds (0 means until Ctrl+C)\n");
    printf("  -d    Enable debug output\n");
    printf("  -v    Enable verbose output\n");
    printf("  -h    Show help\n");
}

static void stop_buffer(void)
{
    pthread_mutex_lock(&sms_buffer.mutex);
    sms_buffer.stopped = true;
    pthread_cond_broadcast(&sms_buffer.not_empty);
    pthread_cond_broadcast(&sms_buffer.not_full);
    pthread_mutex_unlock(&sms_buffer.mutex);
}

int main(int argc, char **argv)
{
    int option;

    while ((option = getopt(argc, argv, "i:t:dvh")) != -1) {
        char *end = NULL;
        long value;

        switch (option) {
        case 'i':
            errno = 0;
            value = strtol(optarg, &end, 10);
            if (errno || end == optarg || *end != '\0' ||
                value < 10 || value > 10000) {
                fprintf(stderr, "Invalid interval: %s\n", optarg);
                return EXIT_FAILURE;
            }
            sms_config.interval_ms = (int)value;
            break;

        case 't':
            errno = 0;
            value = strtol(optarg, &end, 10);
            if (errno || end == optarg || *end != '\0' || value < 0) {
                fprintf(stderr, "Invalid duration: %s\n", optarg);
                return EXIT_FAILURE;
            }
            sms_config.duration_seconds = (int)value;
            break;

        case 'd':
            sms_config.debug = true;
            break;

        case 'v':
            sms_config.verbose = true;
            break;

        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;

        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("SMS Sensor Monitoring System\n");
    printf("Interval: %d ms\n", sms_config.interval_ms);
    printf("Duration: %s\n",
           sms_config.duration_seconds > 0 ? "limited" : "until Ctrl+C");

    if (sms_queues_init() != 0) {
        fprintf(stderr, "Failed to initialize POSIX message queues\n");
        return EXIT_FAILURE;
    }

    pthread_t sensor_tid, processor_tid, alert_tid, logger_tid;
    int sensor_started = 0, processor_started = 0;
    int alert_started = 0, logger_started = 0;

    if (pthread_create(&logger_tid, NULL, logger_thread, NULL) == 0)
        logger_started = 1;
    else
        goto startup_failed;

    if (pthread_create(&alert_tid, NULL, alert_thread, NULL) == 0)
        alert_started = 1;
    else
        goto startup_failed;

    if (pthread_create(&processor_tid, NULL, processor_thread, NULL) == 0)
        processor_started = 1;
    else
        goto startup_failed;

    if (pthread_create(&sensor_tid, NULL, sensor_thread, NULL) == 0)
        sensor_started = 1;
    else
        goto startup_failed;

    if (sms_config.duration_seconds > 0) {
        struct timespec remaining = {
            .tv_sec = sms_config.duration_seconds,
            .tv_nsec = 0
        };

        while (sms_running && nanosleep(&remaining, &remaining) == -1 &&
               errno == EINTR) {
            /* Resume waiting after a signal. */
        }

        sms_running = 0;
    } else {
        while (sms_running)
            pause();
    }

    stop_buffer();

    /*
     * Joining sensor first lets its sleep finish. Queue workers use timed
     * waits below; send wake-up messages so they can exit.
     */
    pthread_join(sensor_tid, NULL);
    pthread_join(processor_tid, NULL);

    /* Wake the alert worker so it can exit cleanly. */
    sms_queues_stop_workers();

    if (alert_started)
        pthread_join(alert_tid, NULL);
    if (logger_started)
        pthread_join(logger_tid, NULL);

    sms_queues_cleanup();
    printf("SMS application stopped.\n");
    return EXIT_SUCCESS;

startup_failed:
    fprintf(stderr, "Failed to create a worker thread\n");
    sms_running = 0;
    stop_buffer();

    if (sensor_started)
        pthread_join(sensor_tid, NULL);
    if (processor_started)
        pthread_join(processor_tid, NULL);
    if (alert_started)
        pthread_cancel(alert_tid);
    if (logger_started)
        pthread_cancel(logger_tid);
    if (alert_started)
        pthread_join(alert_tid, NULL);
    if (logger_started)
        pthread_join(logger_tid, NULL);

    sms_queues_cleanup();
    return EXIT_FAILURE;
}
