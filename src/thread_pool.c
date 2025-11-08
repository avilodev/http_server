#include "thread_pool.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Work queue node
struct WorkItem {
    work_func_t func;
    void* arg;
    struct WorkItem* next;
};

// Thread pool structure
struct ThreadPool {
    pthread_t* threads;
    int num_threads;
    
    // Work queue
    struct WorkItem* work_queue_head;
    struct WorkItem* work_queue_tail;
    int queue_size;
    int max_queue_size;
    
    // Synchronization
    pthread_mutex_t queue_mutex;
    pthread_cond_t work_available;
    pthread_cond_t work_done;
    
    // State
    bool shutdown;
    int active_workers;
    
    // Statistics
    int completed_work;
    int rejected_work;
};

/**
 * Worker thread main loop - processes work items from queue
 *
 * Continuously waits for work items in the thread pool queue and executes
 * them. Each worker thread runs this function in an infinite loop until
 * the pool is shut down. Properly handles synchronization using mutex locks
 * and condition variables to coordinate with other threads.
 *
 * @param arg Pointer to ThreadPool structure (cast from void*)
 *
 * @return NULL when thread exits (on pool shutdown)
 *
 * @note Function runs indefinitely until pool->shutdown is set to true
 * @note Updates active_workers count while executing work items
 * @note Signals work_done condition variable after completing each item
 *
 * @see threadpool_create(), threadpool_add_work()
 */
static void* worker_thread(void* arg) {
    struct ThreadPool* pool = (struct ThreadPool*)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Wait for work or shutdown signal
        while (pool->work_queue_head == NULL && !pool->shutdown) {
            pthread_cond_wait(&pool->work_available, &pool->queue_mutex);
        }
        
        // Check for shutdown
        if (pool->shutdown && pool->work_queue_head == NULL) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }
        
        // Get work item from queue
        struct WorkItem* item = pool->work_queue_head;
        if (item) {
            pool->work_queue_head = item->next;
            if (pool->work_queue_tail == item) {
                pool->work_queue_tail = NULL;
            }
            pool->queue_size--;
            pool->active_workers++;
        }
        
        pthread_mutex_unlock(&pool->queue_mutex);
        
        // Execute work (outside of lock to allow other threads to run)
        if (item) {
            item->func(item->arg);
            free(item);
            
            // Update statistics
            pthread_mutex_lock(&pool->queue_mutex);
            pool->active_workers--;
            pool->completed_work++;
            pthread_cond_signal(&pool->work_done);
            pthread_mutex_unlock(&pool->queue_mutex);
        }
    }
    
    return NULL;
}

/**
 * Creates and initializes a new thread pool
 *
 * Allocates and initializes a thread pool with the specified configuration.
 * Creates worker threads, initializes synchronization primitives (mutexes
 * and condition variables), and sets up the work queue. All worker threads
 * are started immediately and begin waiting for work.
 *
 * @param config ThreadPoolConfig structure containing:
 *               - num_threads: Number of worker threads to create
 *               - max_queue_size: Maximum queued work items (0 = unlimited)
 *
 * @return Pointer to initialized ThreadPool structure, or NULL on error
 *
 * @note Returns NULL if num_threads <= 0 or allocation fails
 * @note All created threads are joined and resources freed on partial failure
 * @note Prints confirmation message on successful creation
 * @warning Caller must destroy pool with threadpool_destroy() when done
 *
 * @see threadpool_destroy(), threadpool_add_work()
 */
struct ThreadPool* threadpool_create(struct ThreadPoolConfig config) {
    if (config.num_threads <= 0) {
        fprintf(stderr, "Invalid thread count: %d\n", config.num_threads);
        return NULL;
    }
    
    struct ThreadPool* pool = calloc(1, sizeof(struct ThreadPool));
    if (!pool) {
        perror("Failed to allocate thread pool");
        return NULL;
    }
    
    pool->num_threads = config.num_threads;
    pool->max_queue_size = config.max_queue_size;
    pool->shutdown = false;
    pool->active_workers = 0;
    pool->completed_work = 0;
    pool->rejected_work = 0;
    
    // Initialize synchronization primitives
    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0) {
        perror("Mutex init failed");
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->work_available, NULL) != 0) {
        perror("Condition variable init failed");
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->work_done, NULL) != 0) {
        perror("Condition variable init failed");
        pthread_cond_destroy(&pool->work_available);
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }
    
    // Create worker threads
    pool->threads = calloc(pool->num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        perror("Failed to allocate thread array");
        pthread_cond_destroy(&pool->work_done);
        pthread_cond_destroy(&pool->work_available);
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }
    
    for (int i = 0; i < pool->num_threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread, pool) != 0) {
            perror("Failed to create worker thread");
            pool->shutdown = true;
            pthread_cond_broadcast(&pool->work_available);
            
            // Wait for already created threads
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            
            free(pool->threads);
            pthread_cond_destroy(&pool->work_done);
            pthread_cond_destroy(&pool->work_available);
            pthread_mutex_destroy(&pool->queue_mutex);
            free(pool);
            return NULL;
        }
    }
    
    printf("Thread pool created with %d worker threads\n", pool->num_threads);
    return pool;
}

/**
 * Adds a work item to the thread pool queue
 *
 * Queues a function and its argument for execution by an available worker
 * thread. If the queue is at maximum capacity, the work is rejected.
 * Work items are executed in FIFO order by the first available thread.
 *
 * @param pool Pointer to ThreadPool structure
 * @param func Function pointer to execute (work_func_t signature)
 * @param arg Argument to pass to the function (can be NULL)
 *
 * @return 0 on success, -1 on failure
 *
 * @note Returns -1 if pool is NULL, func is NULL, allocation fails,
 *       pool is shutting down, or queue is full
 * @note Increments rejected_work counter when queue is full
 * @note Signals one waiting worker thread when work is added
 *
 * @see threadpool_create(), threadpool_wait()
 */
int threadpool_add_work(struct ThreadPool* pool, work_func_t func, void* arg) {
    if (!pool || !func) {
        return -1;
    }
    
    struct WorkItem* item = malloc(sizeof(struct WorkItem));
    if (!item) {
        perror("Failed to allocate work item");
        return -1;
    }
    
    item->func = func;
    item->arg = arg;
    item->next = NULL;
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    // Check if shutting down
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->queue_mutex);
        free(item);
        return -1;
    }
    
    // Check queue size limit
    if (pool->max_queue_size > 0 && pool->queue_size >= pool->max_queue_size) {
        pthread_mutex_unlock(&pool->queue_mutex);
        pool->rejected_work++;
        free(item);
        fprintf(stderr, "Work queue full, rejecting work\n");
        return -1;
    }
    
    // Add to queue
    if (pool->work_queue_tail) {
        pool->work_queue_tail->next = item;
    } else {
        pool->work_queue_head = item;
    }
    pool->work_queue_tail = item;
    pool->queue_size++;
    
    // Signal a worker thread
    pthread_cond_signal(&pool->work_available);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return 0;
}

/**
 * Waits for all queued and active work to complete
 *
 * Blocks the calling thread until the work queue is empty and all worker
 * threads have finished their current tasks. Does not prevent new work
 * from being added during the wait.
 *
 * @param pool Pointer to ThreadPool structure
 *
 * @note Returns immediately if pool is NULL
 * @note Does not shut down the pool - workers remain ready for new work
 * @note Useful for synchronization points in multi-phase processing
 *
 * @see threadpool_add_work(), threadpool_destroy()
 */
void threadpool_wait(struct ThreadPool* pool) {
    if (!pool) return;
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    while (pool->work_queue_head != NULL || pool->active_workers > 0) {
        pthread_cond_wait(&pool->work_done, &pool->queue_mutex);
    }
    
    pthread_mutex_unlock(&pool->queue_mutex);
}

/**
 * Shuts down and destroys a thread pool
 *
 * Signals all worker threads to shut down, waits for them to complete
 * their current work, and frees all resources including threads,
 * synchronization primitives, and remaining queued work items. Prints
 * statistics about completed and rejected work.
 *
 * @param pool Pointer to ThreadPool structure to destroy
 *
 * @note Returns immediately if pool is NULL
 * @note Broadcasts shutdown signal to wake all waiting threads
 * @note Joins all worker threads before cleanup
 * @note Frees work item arguments (assumes they were malloc'd)
 * @note Prints completion statistics before destruction
 * @warning After calling, pool pointer is invalid and must not be used
 *
 * @see threadpool_create(), threadpool_wait()
 */
void threadpool_destroy(struct ThreadPool* pool) {
    if (!pool) return;
    
    // Signal shutdown
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown = true;
    pthread_cond_broadcast(&pool->work_available);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    // Wait for all threads to finish
    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // Free remaining work items
    struct WorkItem* item = pool->work_queue_head;
    while (item) {
        struct WorkItem* next = item->next;
        free(item->arg);  // Assumes arg was malloc'd
        free(item);
        item = next;
    }
    
    // Cleanup
    free(pool->threads);
    pthread_cond_destroy(&pool->work_done);
    pthread_cond_destroy(&pool->work_available);
    pthread_mutex_destroy(&pool->queue_mutex);
    
    printf("Thread pool destroyed. Completed: %d, Rejected: %d\n",
           pool->completed_work, pool->rejected_work);
    
    free(pool);
}

/**
 * Retrieves current thread pool statistics
 *
 * Thread-safe retrieval of pool statistics including active workers,
 * queued work items, and counters for completed and rejected work.
 * Provides a snapshot of the pool's current state.
 *
 * @param pool Pointer to ThreadPool structure
 * @param stats Pointer to ThreadPoolStats structure to fill with data
 *
 * @note Returns immediately if pool or stats is NULL
 * @note Statistics include:
 *       - active_threads: Workers currently executing tasks
 *       - queued_work: Items waiting in queue
 *       - completed_work: Total tasks completed since creation
 *       - rejected_work: Tasks rejected due to full queue
 * @note Thread-safe - uses mutex to ensure consistent snapshot
 *
 * @see threadpool_create(), threadpool_add_work()
 */
void threadpool_get_stats(struct ThreadPool* pool, struct ThreadPoolStats* stats) {
    if (!pool || !stats) return;
    
    pthread_mutex_lock(&pool->queue_mutex);
    stats->active_threads = pool->active_workers;
    stats->queued_work = pool->queue_size;
    stats->completed_work = pool->completed_work;
    stats->rejected_work = pool->rejected_work;
    pthread_mutex_unlock(&pool->queue_mutex);
}