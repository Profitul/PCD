#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"
#include "job.h"

typedef struct job_node {
    job_t *job;
    struct job_node *next;
} job_node_t;

typedef struct job_queue {
    job_node_t *head;
    job_node_t *tail;
    bool stopped;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} job_queue_t;

int job_queue_init(job_queue_t *queue);
void job_queue_destroy(job_queue_t *queue);
int job_queue_push(job_queue_t *queue, job_t *job);
job_t *job_queue_pop(job_queue_t *queue);
void job_queue_stop(job_queue_t *queue);

#endif