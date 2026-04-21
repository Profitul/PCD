#include "queue.h"

/* Initializeaza coada de joburi: mutex + condition variable.
   Returneaza 0 la succes, -1 daca initializarea primitiveilor de sincronizare esueaza. */
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

/* Elibereaza toate nodurile ramase in coada si distruge primitivele de sincronizare. */
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

/* Adauga un job la coada (tail insert) si notifica un worker prin condition variable.
   Returneaza -1 daca coada e oprita (stopped) sau alocarea nodului esueaza. */
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

/* Scoate primul job din coada (head pop), blocheaza daca coada e goala.
   Returneaza NULL daca coada a fost oprita (semnal pentru worker sa se opreasca). */
job_t *job_queue_pop(job_queue_t *queue) {
    if (queue == NULL) {
        return NULL;
    }

    (void)pthread_mutex_lock(&queue->mutex);

    /* Asteptam pana apare un job sau coada e oprita */
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

/* Marcheaza coada ca oprita si trezeste toti workerii blocati in pop. */
void job_queue_stop(job_queue_t *queue) {
    if (queue == NULL) {
        return;
    }

    (void)pthread_mutex_lock(&queue->mutex);
    queue->stopped = true;
    (void)pthread_cond_broadcast(&queue->cond);
    (void)pthread_mutex_unlock(&queue->mutex);
}