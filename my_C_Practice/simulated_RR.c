#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#define NUM_PROD 1
#define NUM_CONS 7
#define QUANTA 5

static int next_job_id = 0;

enum policy { FCFS, SJF, PRIORITY, RR };

typedef struct job {
    int id;
    char *payload;
    int priority;
    int cost;                 
    struct timespec arrival_time;
} Job;

typedef struct ReadySet {
    Job **jobs;
    size_t cap;
    size_t count;
    pthread_mutex_t mtx;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    enum policy policy;
} ReadySet;

static void rs_init(ReadySet *rs, size_t cap) {
    rs->jobs = malloc(cap * sizeof *rs->jobs);
    if (!rs->jobs) {
        perror("malloc jobs");
        exit(EXIT_FAILURE);
    }
    rs->cap = cap;
    rs->count = 0;
    rs->policy = FCFS;   

    pthread_mutex_init(&rs->mtx, NULL);
    pthread_cond_init(&rs->not_full, NULL);
    pthread_cond_init(&rs->not_empty, NULL);
}

static void rs_destroy(ReadySet *rs) {
    pthread_cond_destroy(&rs->not_full);
    pthread_cond_destroy(&rs->not_empty);
    pthread_mutex_destroy(&rs->mtx);
    free(rs->jobs);
}

static void insertJob(ReadySet *rs, Job *job) {
    pthread_mutex_lock(&rs->mtx);
    while (rs->count == rs->cap) {
        pthread_cond_wait(&rs->not_full, &rs->mtx);
    }
    rs->jobs[rs->count] = job;
    rs->count++;
    pthread_cond_signal(&rs->not_empty);
    pthread_mutex_unlock(&rs->mtx);
}

static Job *removeJob(ReadySet *rs) {
    pthread_mutex_lock(&rs->mtx);
    while (rs->count == 0) {
        pthread_cond_wait(&rs->not_empty, &rs->mtx);
    }

    size_t best = 0;

    switch (rs->policy) {
    case FCFS:
        best = 0;
        break;

    case SJF:
        best = 0;
        for (size_t i = 1; i < rs->count; i++) {
            if (rs->jobs[i]->cost < rs->jobs[best]->cost) {
                best = i;
            }
        }
        break;

    case PRIORITY:
        best = 0;
        for (size_t i = 1; i < rs->count; i++) {
            if (rs->jobs[i]->priority > rs->jobs[best]->priority) {
                best = i;
            }
        }
        break;

    case RR:
        best = 0;
        break;

    default:
        pthread_mutex_unlock(&rs->mtx);
        fprintf(stderr, "Unknown policy\n");
        exit(EXIT_FAILURE);
    }

    Job *job = rs->jobs[best];
    rs->jobs[best] = rs->jobs[rs->count - 1];
    rs->count--;

    pthread_cond_signal(&rs->not_full);
    pthread_mutex_unlock(&rs->mtx);
    return job;
}

static void *producer(void *arg) {
    ReadySet *rs = arg;
    char buf[256];

    while (fgets(buf, sizeof buf, stdin)) {
        char *copy = strdup(buf);
        if (!copy) {
            perror("strdup");
            exit(EXIT_FAILURE);
        }

        Job *job = malloc(sizeof *job);
        if (!job) {
            perror("malloc job");
            exit(EXIT_FAILURE);
        }

        job->id = next_job_id++;
        job->payload = copy;
        job->cost = rand() % 10 + 1;       // "burst time"
        job->priority = rand() % 100 + 1;
        clock_gettime(CLOCK_MONOTONIC, &job->arrival_time);

        insertJob(rs, job);
    }

    return NULL;
}

static void *consumer(void *arg) {
    ReadySet *rs = arg;
    int current_time = 0; 

    for (;;) {
        Job *job = removeJob(rs);
        if (job->payload == NULL) {   
            free(job);
            break;
        }

        if (rs->policy == RR) {
            int slice = (job->cost > QUANTA) ? QUANTA : job->cost;

            printf("Job %d ran from %d to %d (remaining %d)\n",
                   job->id, current_time, current_time + slice,
                   job->cost - slice);

            job->cost      -= slice;
            current_time   += slice;

            if (job->cost > 0) {

                insertJob(rs, job);
            } else {
                printf("Job %d finished at time %d\n",
                       job->id, current_time);
                free(job->payload);
                free(job);
            }
        } else {
           
            printf("Job %d ran from %d to %d (finished)\n",
                   job->id, current_time, current_time + job->cost);

            current_time += job->cost;
            free(job->payload);
            free(job);
        }
    }

    return NULL;
}

int main(void) {
    srand((unsigned)time(NULL));

    ReadySet rs;
    rs_init(&rs, 1024);

    pthread_t prod_threads[NUM_PROD];
    pthread_t cons_threads[NUM_CONS];

    int choice;
    printf("Choose scheduling policy:\n");
    printf("0 = FCFS\n1 = SJF\n2 = PRIORITY\n3 = RR\n> ");
    if (scanf("%d", &choice) != 1) {
        fprintf(stderr, "Invalid input, defaulting to FCFS\n");
        choice = 0;
    }

    if      (choice == 0) rs.policy = FCFS;
    else if (choice == 1) rs.policy = SJF;
    else if (choice == 2) rs.policy = PRIORITY;
    else if (choice == 3) rs.policy = RR;
    else                  rs.policy = FCFS;

    for (int k = 0; k < NUM_PROD; ++k) {
        if (pthread_create(&prod_threads[k], NULL, producer, &rs) != 0) {
            perror("pthread_create producer");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < NUM_CONS; ++i) {
        if (pthread_create(&cons_threads[i], NULL, consumer, &rs) != 0) {
            perror("pthread_create consumer");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for producer to finish reading stdin
    for (int k = 0; k < NUM_PROD; ++k) {
        pthread_join(prod_threads[k], NULL);
    }

   
    for (int i = 0; i < NUM_CONS; ++i) {
        Job *poison = malloc(sizeof *poison);
        if (!poison) {
            perror("malloc poison");
            exit(EXIT_FAILURE);
        }
        poison->id = -1;
        poison->payload = NULL;
        poison->cost = 0;
        poison->priority = 0;
        insertJob(&rs, poison);
    }

    for (int i = 0; i < NUM_CONS; ++i) {
        pthread_join(cons_threads[i], NULL);
    }

    rs_destroy(&rs);
    return 0;
}
