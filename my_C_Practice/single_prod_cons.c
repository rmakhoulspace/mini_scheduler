#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>


typedef struct{
    char **buf;
    size_t cap;
    size_t count;
    size_t head;
    size_t tail;
    pthread_mutex_t mtx;
    pthread_cond_t not_full;
    pthread_cond_t not_null;
} BoundedBuffer;

void insertJob(BoundedBuffer *q, char *line){

    pthread_mutex_lock(&q->mtx);
    while(q->count == q->cap){
        pthread_cond_wait(&q->not_full, &q->mtx);
    }

    q->buf[q->tail] = line;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;

    pthread_cond_signal(&q->not_null);
    pthread_mutex_unlock(&q->mtx);
}

char *removeJob(BoundedBuffer *q){

    pthread_mutex_lock(&q->mtx);
    while(q->count == 0){
        pthread_cond_wait(&q->not_null, &q->mtx);
    }

    char *line = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mtx);

    return line;
}

void *producer(void *arg){

    BoundedBuffer *q = arg;
    char buf[256];

    while(fgets(buf, sizeof(buf), stdin)){

        char *copy = strdup(buf);
        if(!copy){
            perror("strdup");
            exit(EXIT_FAILURE);
        }
        insertJob(q, copy);
    }

        insertJob(q, NULL);
        return NULL;

}

void *consumer(void *arg){

    BoundedBuffer *q = arg;

    for(;;){
        char *line = removeJob(q);
        if(line == NULL) break;
        fputs(line, stdout);
        free(line);
    }

    return NULL;
}

void q_init(BoundedBuffer *q, size_t cap){
    q->buf = malloc(cap *sizeof *q->buf);
    if(!q->buf){
        perror("malloc");
        exit(1);
    }

    q->cap = cap;
    q->count = q->head = q->tail = 0;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_null, NULL);
}

void q_destroy(BoundedBuffer *q){
    pthread_cond_destroy(&q->not_null);
    pthread_cond_destroy(&q->not_full);
    pthread_mutex_destroy(&q->mtx);
    free(q->buf);
}

int main(void){

        BoundedBuffer q;
        q_init(&q, 1024);
        pthread_t prod, cons;

        pthread_create(&prod, NULL, producer, &q);
        pthread_create(&cons, NULL, consumer, &q);

        pthread_join(prod, NULL);
        pthread_join(cons, NULL);

        q_destroy(&q);

        return 0;
}