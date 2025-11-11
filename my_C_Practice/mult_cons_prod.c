#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#define NUM_PROD 1        
#define NUM_CONS 7      

typedef struct {
    char **buf;
    size_t cap;
    size_t count;
    size_t head; 
    size_t tail; 
    pthread_mutex_t mtx;
    pthread_cond_t  not_full;   
    pthread_cond_t  not_empty;  
} BoundedBuffer;


static void insertJob(BoundedBuffer *q, char *line) {
    pthread_mutex_lock(&q->mtx);
    while (q->count == q->cap) {
        pthread_cond_wait(&q->not_full, &q->mtx);
    }
    q->buf[q->tail] = line;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    pthread_cond_signal(&q->not_empty);  
    pthread_mutex_unlock(&q->mtx);
}


static char *removeJob(BoundedBuffer *q) {
    pthread_mutex_lock(&q->mtx);
    while (q->count == 0) {
        pthread_cond_wait(&q->not_empty, &q->mtx);
    }
    char *line = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    pthread_cond_signal(&q->not_full);  
    pthread_mutex_unlock(&q->mtx);
    return line;
}

static void *producer(void *arg) {
    BoundedBuffer *q = arg;
    char buf[256];

    while (fgets(buf, sizeof buf, stdin)) {
        char *copy = strdup(buf);
        if (!copy) {
            perror("strdup");
            exit(EXIT_FAILURE);
        }
        insertJob(q, copy);
    }
    return NULL;
}

static void *consumer(void *arg) {
    BoundedBuffer *q = arg;
    for (;;) {
        char *line = removeJob(q);
        if (line == NULL) {
            break;
        }
        fputs(line, stdout);
        free(line);
    }
    return NULL;
}

static void q_init(BoundedBuffer *q, size_t cap) {
    q->buf = malloc(cap * sizeof *q->buf);
    if (!q->buf) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    q->cap = cap;
    q->count = 0;
    q->head = 0;
    q->tail = 0;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
}

static void q_destroy(BoundedBuffer *q) {
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_mutex_destroy(&q->mtx);
    free(q->buf);
}

int main(void) {
    BoundedBuffer q;
    q_init(&q, 1024);

    pthread_t prod_threads[NUM_PROD];
    pthread_t cons_threads[NUM_CONS];

    for (int k = 0; k < NUM_PROD; ++k) {
        if (pthread_create(&prod_threads[k], NULL, producer, &q) != 0) {
            perror("pthread_create producer");
            exit(EXIT_FAILURE);
        }
    }

    
    for (int i = 0; i < NUM_CONS; ++i) {
        if (pthread_create(&cons_threads[i], NULL, consumer, &q) != 0) {
            perror("pthread_create consumer");
            exit(EXIT_FAILURE);
        }
    }

    
    for (int k = 0; k < NUM_PROD; ++k) {
        pthread_join(prod_threads[k], NULL);
    }

    for (int i = 0; i < NUM_CONS; ++i) {
        insertJob(&q, NULL);
    }

    for (int i = 0; i < NUM_CONS; ++i) {
        pthread_join(cons_threads[i], NULL);
    }

    q_destroy(&q);
    return 0;
}
