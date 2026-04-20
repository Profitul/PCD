#ifndef WORKER_H
#define WORKER_H

#include "common.h"
#include "job.h"
#include "queue.h"

typedef struct worker_event {
    uint64_t job_id;
    job_state_t state;
} worker_event_t;

typedef struct worker_context {
    job_queue_t *queue;
    job_table_t *table;
    int notify_fd;
    volatile sig_atomic_t *running;
    size_t worker_index;
} worker_context_t; 

void *worker_main(void *arg);

#endif