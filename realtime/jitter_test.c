#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/mman.h>

#define SAMPLE_COUNT 1000
#define PERIOD_NS 1000000LL

static int64_t to_ns(struct timespec t)
{
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static struct timespec add_ns(struct timespec t, int64_t ns)
{
    t.tv_sec += ns / 1000000000LL;
    t.tv_nsec += ns % 1000000000LL;

    if (t.tv_nsec >= 1000000000L) {
        t.tv_sec++;
        t.tv_nsec -= 1000000000L;
    }

    return t;
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr,
                "Usage: %s <OTHER|FIFO> <output.csv>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    int policy;
    int priority = 0;

    if (strcmp(argv[1], "FIFO") == 0) {
        policy = SCHED_FIFO;
        priority = 50;
    } else if (strcmp(argv[1], "OTHER") == 0) {
        policy = SCHED_OTHER;
    } else {
        fprintf(stderr, "Policy must be OTHER or FIFO\n");
        return EXIT_FAILURE;
    }

    /*
     * Lock current and future memory pages.
     */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        fprintf(stderr, "Warning: mlockall failed: %s\n",
                strerror(errno));
    } else {
        printf("mlockall: success\n");
    }

    struct sched_param param = { .sched_priority = priority };

    if (sched_setscheduler(0, policy, &param) == -1) {
        fprintf(stderr, "sched_setscheduler failed: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    FILE *fp = fopen(argv[2], "w");
    if (!fp) {
        perror("fopen output");
        return EXIT_FAILURE;
    }

    fprintf(fp, "sample,target_ns,actual_ns,jitter_ns\n");

    struct timespec target;

    if (clock_gettime(CLOCK_MONOTONIC, &target) == -1) {
        perror("clock_gettime");
        fclose(fp);
        return EXIT_FAILURE;
    }

    /* Start one period after the current time. */
    target = add_ns(target, PERIOD_NS);

    int64_t min_jitter = INT64_MAX;
    int64_t max_jitter = INT64_MIN;
    long double sum_jitter = 0;

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        target = add_ns(target, PERIOD_NS);

        int rc;
        do {
            rc = clock_nanosleep(
                CLOCK_MONOTONIC,
                TIMER_ABSTIME,
                &target,
                NULL
            );
        } while (rc == EINTR);

        if (rc != 0) {
            fprintf(stderr, "clock_nanosleep failed: %s\n",
                    strerror(rc));
            fclose(fp);
            return EXIT_FAILURE;
        }

        struct timespec actual;

        if (clock_gettime(CLOCK_MONOTONIC, &actual) == -1) {
            perror("clock_gettime");
            fclose(fp);
            return EXIT_FAILURE;
        }

        int64_t target_ns = to_ns(target);
        int64_t actual_ns = to_ns(actual);
        int64_t jitter = actual_ns - target_ns;

        if (jitter < min_jitter)
            min_jitter = jitter;

        if (jitter > max_jitter)
            max_jitter = jitter;

        sum_jitter += jitter;

        fprintf(fp, "%d,%" PRId64 ",%" PRId64 ",%" PRId64 "\n",
                i + 1, target_ns, actual_ns, jitter);
    }

    fclose(fp);

    printf("\n=== Real-Time Jitter Results ===\n");
    printf("Policy: %s\n", argv[1]);
    printf("Samples: %d\n", SAMPLE_COUNT);
    printf("Period: 1 ms\n");
    printf("Minimum jitter: %" PRId64 " ns\n", min_jitter);
    printf("Maximum jitter: %" PRId64 " ns\n", max_jitter);
    printf("Average jitter: %.2Lf ns\n",
           sum_jitter / SAMPLE_COUNT);
    printf("CSV output: %s\n", argv[2]);

    return EXIT_SUCCESS;
}
