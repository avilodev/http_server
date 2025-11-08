#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>

// Forward declarations
struct ThreadPool;
struct WorkItem;

// Work function signature
typedef void* (*work_func_t)(void* arg);

// Configuration
struct ThreadPoolConfig {
    int num_threads;        // Number of worker threads
    int max_queue_size;     // Max pending work items (0 = unlimited)
};

// Thread pool operations
struct ThreadPool* threadpool_create(struct ThreadPoolConfig config);
int threadpool_add_work(struct ThreadPool* pool, work_func_t func, void* arg);
void threadpool_wait(struct ThreadPool* pool);
void threadpool_destroy(struct ThreadPool* pool);

// Statistics (optional but useful)
struct ThreadPoolStats {
    int active_threads;
    int queued_work;
    int completed_work;
    int rejected_work;
};

void threadpool_get_stats(struct ThreadPool* pool, struct ThreadPoolStats* stats);

#endif