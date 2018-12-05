#include "thread_pool.h"
#include <stdio.h>


static pthread_mutex_t mutex;
static sem_t sem_queue_use, sem_queue_remain;
static struct Queue queue;
static struct Threads threads;


static int queue_init(int queue_size)
{
    if (queue_size <= 0)
        return -1;

    if (pthread_mutex_init(&mutex, NULL) != 0) {
        return -1;
    }
    if (sem_init(&sem_queue_use, 0, 0) < 0){
        pthread_mutex_destroy(&mutex);
        return -1;
    }
    if (sem_init(&sem_queue_remain, 0, queue_size) < 0){
        pthread_mutex_destroy(&mutex);
        sem_destroy(&sem_queue_use);
        return -1;
    }

    queue.queue = (void **)malloc(queue_size * sizeof(void *));
    if (queue.queue == NULL) {
        pthread_mutex_destroy(&mutex);
        sem_destroy(&sem_queue_use);
        sem_destroy(&sem_queue_remain);
        return -1;
    }
    queue.size = queue_size;
    queue.now = 0;
    return 0;
}

static void queue_destroy()
{
    pthread_mutex_destroy(&mutex);
    sem_destroy(&sem_queue_use);
    sem_destroy(&sem_queue_remain);
    free(queue.queue);
}

static void queue_push(void *data)
{
    sem_wait(&sem_queue_remain);
    pthread_mutex_lock(&mutex);
    queue.queue[queue.now++] = data;
    pthread_mutex_unlock(&mutex);
    sem_post(&sem_queue_use);

    return;
}

static void *queue_pop()
{
    void *data;

    sem_wait(&sem_queue_use);
    pthread_mutex_lock(&mutex);
    data = queue.queue[--queue.now];
    pthread_mutex_unlock(&mutex);
    sem_post(&sem_queue_remain);

    return data;
}


static void *thread_func(void *work_func)
{
    pthread_detach(pthread_self());
    while (1) {
        void *data = queue_pop();
        (*(void *(*)(void *))work_func)(data);
    }

    return NULL;
}

static int threads_init(void *(*work_func)(void *), const int pool_size)
{
    if (pool_size <= 0) {
        return -1;
    }
    threads.threads = (pthread_t *)malloc(sizeof(pthread_t *) * pool_size);
    if (threads.threads == NULL) {
        return -1;
    }
    for (threads.size = 0; threads.size < pool_size; threads.size++) {
        if (pthread_create(threads.threads + threads.size, NULL, thread_func, (void *)work_func) != 0)
            break;
    }
    if (threads.size != pool_size) {
        for (int i = 0; i < threads.size; i++)
            pthread_cancel(*(threads.threads+i));
        free(threads.threads);
        return -1;
    }
    
    return 0;
}

static void threads_destroy()
{
    for (int i = 0; i < threads.size; i++)
        pthread_cancel(*(threads.threads+i));
    free(threads.threads);
    return;
}


int thread_pool_init(void *(*work_func)(void *), const int pool_size, int queue_size)
{
    if (queue_init(queue_size) != 0) {
        return -1;
    }
    if (threads_init(work_func, pool_size) != 0) {
        return -1;
    }
    return 0;
}

int thread_pool_put(void *data)
{
    queue_push(data);
}

void thread_pool_destroy()
{
    threads_destroy();
    queue_destroy();
}
