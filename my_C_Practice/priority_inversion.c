#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

static pthread_mutex_t lock;


typedef struct Scenario {
    int mitigation_enabled;         // 0 = baseline, 1 = mitigation
    volatile int high_waiting;      // set by high thread when blocked
    struct timespec t_start;
    struct timespec t_end;
    double wait_ms;
} Scenario;

static double diff_ms(struct timespec a, struct timespec b) {
    double sec  = (double)(b.tv_sec  - a.tv_sec);
    double nsec = (double)(b.tv_nsec - a.tv_nsec);
    return sec * 1000.0 + nsec / 1e6;
}

static void ms_sleep(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void busy_cpu_loop(unsigned long iters) {
    volatile unsigned long x = 0;
    for (unsigned long i = 0; i < iters; ++i) {
        x += i;
    }
}

/* bind thread to CPU 0 so they really compete on one core */
static void pin_to_cpu0(void) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

static void busy_work_ms(Scenario *s, int total_ms, int allow_yield) {
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        busy_cpu_loop(100000);  

        clock_gettime(CLOCK_MONOTONIC, &now);
        if (diff_ms(start, now) >= total_ms)
            break;

        if (allow_yield && s->mitigation_enabled && s->high_waiting) {
            //let low run so it can release the lock
            struct timespec tiny = {0, 1000000L};
            nanosleep(&tiny, NULL);
        }
    }
}



static void *low_thread(void *arg) {
    Scenario *s = arg;
    (void)s;
    pin_to_cpu0();

    pthread_mutex_lock(&lock);
    printf("L: got lock, doing long critical section...\n");
    busy_work_ms(s, 500, 0);  
    pthread_mutex_unlock(&lock);
    printf("L: released lock\n");
    return NULL;
}

static void *high_thread(void *arg) {
    Scenario *s = arg;
    pin_to_cpu0();

   
    ms_sleep(50);

    clock_gettime(CLOCK_MONOTONIC, &s->t_start);
    s->high_waiting = 1;
    printf("H: trying to get lock...\n");

    pthread_mutex_lock(&lock); 
    clock_gettime(CLOCK_MONOTONIC, &s->t_end);
    s->high_waiting = 0;

    s->wait_ms = diff_ms(s->t_start, s->t_end);
    printf("H: got lock after %.3f ms\n", s->wait_ms);

    pthread_mutex_unlock(&lock);
    return NULL;
}

static void *mid_thread(void *arg) {
    Scenario *s = arg;
    pin_to_cpu0();

    
    ms_sleep(60);
    printf("M: starting heavy independent work (no lock)\n");

    
    busy_work_ms(s, 700, 1);

    printf("M: finished work\n");
    return NULL;
}


static double run_scenario(int mitigation_enabled) {
    Scenario s;
    s.mitigation_enabled = mitigation_enabled;
    s.high_waiting = 0;
    s.wait_ms = 0.0;

    pthread_t t_low, t_mid, t_high;

    if (pthread_mutex_init(&lock, NULL) != 0) {
        perror("pthread_mutex_init");
        exit(1);
    }

    if (pthread_create(&t_low,  NULL, low_thread,  &s) != 0 ||
        pthread_create(&t_high, NULL, high_thread, &s) != 0 ||
        pthread_create(&t_mid,  NULL, mid_thread,  &s) != 0) {
        perror("pthread_create");
        exit(1);
    }

    pthread_join(t_low,  NULL);
    pthread_join(t_mid,  NULL);
    pthread_join(t_high, NULL);

    pthread_mutex_destroy(&lock);

    return s.wait_ms;
}



int main(void) {
    srand((unsigned)time(NULL));

    printf("=== Priority inversion demo (simulated) ===\n");

    printf("\n[1] Baseline: no mitigation\n");
    double baseline = run_scenario(0);

    printf("\n[2] Mitigation: medium thread yields when high is waiting\n");
    double mitigated = run_scenario(1);

    printf("\nSummary:\n");
    printf("  High thread wait (baseline):   %.3f ms\n", baseline);
    printf("  High thread wait (mitigated):  %.3f ms\n", mitigated);
    printf("  Difference:                    %.3f ms\n",
           baseline - mitigated);

    return 0;
}
