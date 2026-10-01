#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>

// Forward declarations
struct thread_pool;
struct work_item;

// Work function signature
typedef void* (*work_func_t)(void* arg);

// Configuration
struct thread_pool_config {
	int num_threads;        // Number of worker threads
	int max_queue_size;     // Max pending work items (0 = unlimited)
};

// Thread pool operations
struct thread_pool* threadpool_create(struct thread_pool_config config);
int threadpool_add_work(struct thread_pool* pool, work_func_t func, void* arg);
void threadpool_wait(struct thread_pool* pool);
void threadpool_destroy(struct thread_pool* pool);

// Statistics (optional but useful)
struct thread_pool_stats {
	int active_threads;
	int queued_work;
	int completed_work;
	int rejected_work;
};

void threadpool_get_stats(struct thread_pool* pool, struct thread_pool_stats* stats);

#endif /* THREAD_POOL_H */