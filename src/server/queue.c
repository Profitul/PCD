#include "queue.h"

int job_queue_init(job_queue_t *queue) {
    if (queue == NULL) {
        return -1;
    }

    (void)memset(queue, 0, sizeof(*queue));

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        return -1;
    }

    if (pthread_cond_init(&queue->cond, NULL) != 0) {
        (void)pthread_mutex_destroy(&queue->mutex);
        return -1;
    }

    return 0;
}

void job_queue_destroy(job_queue_t *queue) {
    if (queue == NULL) {
        return;
    }

    (void)pthread_mutex_lock(&queue->mutex);

    job_node_t *node = queue->head;
    while (node != NULL) {
        job_node_t *next = node->next;
        free(node);
        node = next;
    }

    queue->head = NULL;
    queue->tail = NULL;

    (void)pthread_mutex_unlock(&queue->mutex);

    (void)pthread_mutex_destroy(&queue->mutex);
    (void)pthread_cond_destroy(&queue->cond);
}

int job_queue_push(job_queue_t *queue, job_t *job) {
    if (queue == NULL || job == NULL) {
        return -1;
    }

    job_node_t *node = (job_node_t *)calloc(1U, sizeof(*node));
    if (node == NULL) {
        return -1;
    }

    node->job = job;

    (void)pthread_mutex_lock(&queue->mutex);

    if (queue->stopped) {
        (void)pthread_mutex_unlock(&queue->mutex);
        free(node);
        return -1;
    }

    if (queue->tail == NULL) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }

    (void)pthread_cond_signal(&queue->cond);
    (void)pthread_mutex_unlock(&queue->mutex);

    return 0;
}

job_t *job_queue_pop(job_queue_t *queue) {
    if (queue == NULL) {
        return NULL;
    }

    (void)pthread_mutex_lock(&queue->mutex);

    while (queue->head == NULL && !queue->stopped) {
        (void)pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    if (queue->stopped) {
        (void)pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    job_node_t *node = queue->head;
    queue->head = node->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    (void)pthread_mutex_unlock(&queue->mutex);

    job_t *job = node->job;
    free(node);
    return job;
}

void job_queue_stop(job_queue_t *queue) {
    if (queue == NULL) {
        return;
    }

    (void)pthread_mutex_lock(&queue->mutex);
    queue->stopped = true;
    (void)pthread_cond_broadcast(&queue->cond);
    (void)pthread_mutex_unlock(&queue->mutex);
}