#ifndef WORKER_THREAD_POOL_H
#define WORKER_THREAD_POOL_H

#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>


struct Queue {
    int size;
    int now;
    void **queue;
};

struct Threads {
    int size;
    pthread_t *threads;
};

int thread_pool_init(void *(*work_func)(void *), const int pool_size, int queue_size);

int thread_pool_put(void *data);

void thread_pool_destroy();

#endif
