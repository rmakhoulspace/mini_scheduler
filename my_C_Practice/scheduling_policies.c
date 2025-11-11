#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

#define NUM_PROD 1        
#define NUM_CONS 7      

static int next_job_id = 0;

enum policy { FCFS, SJF, PRIORITY};

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
}ReadySet;


static void insertJob(ReadySet *rs, Job* job) {
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

    if(rs->policy == FCFS) best = 0;

    else if(rs->policy == SJF){
        best = 0;
        for(size_t i = 1; i < rs->count; i++){
            if(rs->jobs[i]->cost < rs->jobs[best]->cost){
                best = i;
            }
        }
    }

    else if(rs->policy == PRIORITY){
        best = 0;
        for(size_t i = 1; i < rs->count; i++){
            if(rs->jobs[i]->priority > rs->jobs[best]->priority){
                best = i;
            }
    }
}
    else {
        pthread_mutex_unlock(&rs->mtx);
        fprintf(stderr, "Unknown Policy");
        exit(EXIT_FAILURE);
    }

    Job *job = rs->jobs[best];
    rs->jobs[best] = rs->jobs[rs->count-1];
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
        job->cost = rand() % 10 + 1;
        job->priority = rand() % 100 + 1;
        clock_gettime(CLOCK_MONOTONIC, &job->arrival_time);
        insertJob(rs, job);
    }
    return NULL;
}

static void *consumer(void *arg) {
    ReadySet *rs = arg;

    for (;;) {
        Job *job = removeJob(rs);   
        if (job->payload == NULL) {
         free(job);
         break;
            }

        fputs(job->payload, stdout);
        free(job->payload);
        free(job);
    }

    return NULL;
}

static void rs_init(ReadySet *rs, size_t cap) {
    rs->jobs = malloc(cap * sizeof *rs->jobs);
    if (!rs->jobs) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    rs->cap = cap;
    rs->count = 0;
    pthread_mutex_init(&rs->mtx, NULL); 
}

static void rs_destroy(ReadySet *rs) {
    pthread_mutex_destroy(&rs->mtx);
    free(rs->jobs);
}

int main(void) {

    ReadySet *rs = malloc(sizeof *rs); 
    if (!rs) {
        perror("malloc rs");
        exit(EXIT_FAILURE); }
  
    rs_init(rs, 1024);

    pthread_t prod_threads[NUM_PROD];
    pthread_t cons_threads[NUM_CONS];

    int choice;
    printf("Kindly pick the scheduling policy:\n");
    printf("0 = FCFS\n1 = SJF\n2 = PRIORITY\n> ");
    if (scanf("%d", &choice) != 1) { 
    fprintf(stderr, "Invalid input. Defaulting to FCFS.\n");
    choice = 0;
}

    if(choice == 0) rs->policy = FCFS;
    if(choice == 1) rs-> policy = SJF;
    if(choice == 2) rs->policy = PRIORITY;

    for (int k = 0; k < NUM_PROD; ++k) {
        if (pthread_create(&prod_threads[k], NULL, producer, rs) != 0) {
            perror("pthread_create producer");
            exit(EXIT_FAILURE);
        }
    }

    
    for (int i = 0; i < NUM_CONS; ++i) {
        if (pthread_create(&cons_threads[i], NULL, consumer, rs) != 0) {
            perror("pthread_create consumer");
            exit(EXIT_FAILURE);
        }
    }

    
    for (int k = 0; k < NUM_PROD; ++k) {
        pthread_join(prod_threads[k], NULL);
    }

    for (int i = 0; i < NUM_CONS; ++i) {
       Job *poison = malloc(sizeof *poison);
        poison->id = -1;
        poison->payload = NULL;
        poison->cost = 0;
        poison->priority = 0;

        insertJob(rs, poison);
    }

    for (int i = 0; i < NUM_CONS; ++i) {
        pthread_join(cons_threads[i], NULL);
    }

    rs_destroy(rs);
    return 0;
}
