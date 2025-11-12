#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NUM_PROD    1
#define NUM_CONS    4
#define NUM_LEVELS  3
#define CAPACITY    1024
#define BOOST_MS    200

typedef struct Job {
    int id;
    char *payload;
    int priority;
    int cost;
} Job;

typedef struct ReadySet {
    Job   **jobs;
    size_t cap, count;
    pthread_mutex_t mtx;
    pthread_cond_t  not_full;
    
} ReadySet;


static ReadySet queues[NUM_LEVELS];
static int      Quanta[NUM_LEVELS] = {5, 10, 20}; 
static pthread_mutex_t any_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  any_not_empty = PTHREAD_COND_INITIALIZER;
static volatile int    running = 1;
static int next_job_id = 0;
static struct timespec last_boost;

static inline int min_int(int a,int b){ return a < b ? a : b; }

static long now_ms(void){
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)(t.tv_sec*1000LL + t.tv_nsec/1000000LL);
}

static void ms_sleep(int ms){
    struct timespec ts={ ms/1000, (ms%1000)*1000000L };
    nanosleep(&ts,NULL);
}


static void rs_init(ReadySet *rs, size_t cap){
    rs->jobs = malloc(cap * sizeof *rs->jobs);
    if(!rs->jobs){ perror("malloc"); exit(1); }
    rs->cap = cap; rs->count = 0;
    pthread_mutex_init(&rs->mtx, NULL);
    pthread_cond_init(&rs->not_full, NULL);
}

static void rs_destroy(ReadySet *rs){
    pthread_cond_destroy(&rs->not_full);
    pthread_mutex_destroy(&rs->mtx);
    free(rs->jobs);
}


static void rs_push(ReadySet *rs, Job *j){
    pthread_mutex_lock(&rs->mtx);
    while(rs->count == rs->cap){
        pthread_cond_wait(&rs->not_full, &rs->mtx);
    }
    rs->jobs[rs->count++] = j;
    pthread_mutex_unlock(&rs->mtx);

    pthread_mutex_lock(&any_mtx);
    pthread_cond_signal(&any_not_empty);
    pthread_mutex_unlock(&any_mtx);
}

static Job* rs_try_pop(ReadySet *rs){
    Job *ret = NULL;
    pthread_mutex_lock(&rs->mtx);
    if(rs->count){
        size_t idx = rs->count - 1;
        ret = rs->jobs[idx];
        rs->count--;
        pthread_cond_signal(&rs->not_full);
    }
    pthread_mutex_unlock(&rs->mtx);
    return ret;
}


static Job* mlfq_pop(int *out_lvl){
    for(;;){
        for(int lvl=0; lvl<NUM_LEVELS; ++lvl){
            Job *j = rs_try_pop(&queues[lvl]);
            if(j){ if(out_lvl) *out_lvl = lvl; return j; }
        }
     
        pthread_mutex_lock(&any_mtx);
        if(!running){ pthread_mutex_unlock(&any_mtx); return NULL; }
        pthread_cond_wait(&any_not_empty, &any_mtx);
        pthread_mutex_unlock(&any_mtx);
    }
}


static void maybe_boost(void){
    long now = now_ms();
    static long last = -1;
    if(last < 0) last = now;
    if(now - last < BOOST_MS) return;
    last = now;


    for(int lvl=1; lvl<NUM_LEVELS; ++lvl){
        for(;;){
            Job *j = rs_try_pop(&queues[lvl]);
            if(!j) break;
            rs_push(&queues[0], j);
        }
    }
}

static void do_slice(int slice_ms){
    ms_sleep(slice_ms);
}

static void* producer(void *arg){
    (void)arg;
    char buf[256];
    while(fgets(buf, sizeof buf, stdin)){
        Job *j = malloc(sizeof *j);
        if(!j){ perror("malloc job"); exit(1); }
        j->id = __sync_fetch_and_add(&next_job_id, 1);
        j->payload = strdup(buf); 
        j->priority = rand()%100 + 1;
        j->cost = rand()%40 + 10; 

      
        rs_push(&queues[0], j);
    }


    int ncons = *(int*)(((void**)arg)[1]); 
    (void)ncons; 
    pthread_mutex_lock(&any_mtx);
    running = 0;
    pthread_cond_broadcast(&any_not_empty);
    pthread_mutex_unlock(&any_mtx);
    return NULL;
}

static void* consumer(void *arg){
    (void)arg;
    for(;;){
        maybe_boost();

        int lvl = -1;
        Job *job = mlfq_pop(&lvl);
        if(!job){
            return NULL;
        }

        int quantum = Quanta[lvl];
        int slice   = min_int(job->cost, quantum);

        do_slice(slice);
        job->cost -= slice;

        if(job->cost <= 0){
            printf("[FIN] job %d (from Q%d)\n", job->id, lvl);
            free(job->payload);
            free(job);
            continue;
        }

        // feedback
        int new_lvl;
        if(slice < quantum){
            new_lvl = (lvl > 0) ? lvl - 1 : 0;
        }else{
            new_lvl = (lvl < NUM_LEVELS-1) ? lvl + 1 : lvl;
        }
        rs_push(&queues[new_lvl], job);
    }
}

int main(void){
    srand((unsigned)time(NULL));

    for(int i=0;i<NUM_LEVELS;++i) rs_init(&queues[i], CAPACITY);

    pthread_t prod[NUM_PROD], cons[NUM_CONS];

    for(int k=0;k<NUM_PROD;++k)
        if(pthread_create(&prod[k], NULL, producer, NULL)!=0){ perror("pthread_create prod"); exit(1); }

    for(int i=0;i<NUM_CONS;++i)
        if(pthread_create(&cons[i], NULL, consumer, NULL)!=0){ perror("pthread_create cons"); exit(1); }

    for(int k=0;k<NUM_PROD;++k) pthread_join(prod[k], NULL);
    pthread_mutex_lock(&any_mtx);
    running = 0;
    pthread_cond_broadcast(&any_not_empty);
    pthread_mutex_unlock(&any_mtx);

    for(int i=0;i<NUM_CONS;++i) pthread_join(cons[i], NULL);

    for(int i=0;i<NUM_LEVELS;++i) rs_destroy(&queues[i]);
    return 0;
}
