/**
 *  \file cqueue.c
 *  \author Francesco Bertolaccini 543981
 * 
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 */

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include "cqueue.h"

#define CHECK_RET if(ret != 0) { \
                    errno = ret; \
                    return -1; \
                  }

/**
 * \brief Nodo di una coda rappresentata come lista linkata
 */
typedef struct node {
  void *v; ///< Valore del nodo
  struct node *next; ///< Nodo successivo
} node_t;

/**
 * \brief Una coda concorrente
 */
struct cqueue {
  node_t *head; ///< Primo elemento della coda
  node_t *tail; ///< Ultimo elemento della coda
  int size; ///< Lunghezza della coda
  pthread_mutex_t mtx; ///< Mutex d'accesso alla coda
  pthread_cond_t cnd_avail; ///< Variabile di condizione per l'attesa di elementi
};

static void freeList(node_t *n, cqueue_deinitializer cb) {
  if(n == NULL) { return; }

  if(n->next != NULL) {
    freeList(n->next, cb);
    n->next = NULL;
  }

  if(cb != NULL) {
    cb(n->v);
    n->v = NULL;
  }

  free(n);
}

cqueue_t *cqueue_init() {
  cqueue_t *cq = calloc(1, sizeof(cqueue_t));
  if(!cq) return NULL;
  int ret;
  cq->head = NULL;
  cq->tail = NULL;
  cq->size = 0;
  if((ret = pthread_mutex_init(&cq->mtx, NULL)) != 0) {
    goto fail;
  }
  if((ret = pthread_cond_init(&cq->cnd_avail, NULL)) != 0) {
    goto fail;
  }

  return cq;

fail:
  errno = ret;
  free(cq);
  return NULL;
}

int cqueue_deinit(cqueue_t *cq, cqueue_deinitializer cb) {
  if(cq == NULL) {
    errno = EINVAL;
    return -1;
  }

  freeList(cq->head, cb);
  
  int ret = 0;
  if((ret = pthread_mutex_destroy(&cq->mtx)) != 0) {
    errno = ret;
    ret = -1;
  }

  if((ret = pthread_cond_destroy(&cq->cnd_avail)) != 0) {
    errno = ret;
    ret = -1;
  }

  free(cq);
  return ret;
}

/**
 * \brief Inserisce un elemento in coda senza ottenere il blocco
 * 
 * \param cq La coda in cui inserire l'elemento
 * \param v L'elemento da inserire
 * \return int 0 in caso di successo, -1 ed errno impostato altrimenti
 */
static int cqueue_push_nolock(cqueue_t *cq, void *v) {
  if(cq->tail == NULL) {
    /* Caso in cui la coda sia ancora vuota */
    cq->head = calloc(1, sizeof(node_t));
    if(cq->head == NULL) {
      return -1;
    }
    cq->tail = cq->head;
    cq->head->v = v;
    cq->head->next = NULL;
    cq->size = 1;
  } else {
    /* Caso in cui la coda abbia più di un elemento */
    node_t *newElem = calloc(1, sizeof(node_t));
    if(newElem == NULL) {
      return -1;
    }
    newElem->next = NULL;
    newElem->v = v;
    
    cq->tail->next = newElem;
    cq->tail = newElem;
    cq->size++;
  }

  int ret = pthread_cond_signal(&cq->cnd_avail);
  CHECK_RET

  return ret;
}

int cqueue_push(cqueue_t *cq, void *v) {
  int ret = pthread_mutex_lock(&cq->mtx);
  CHECK_RET

  ret = cqueue_push_nolock(cq, v);
  CHECK_RET

  ret = pthread_mutex_unlock(&cq->mtx);
  CHECK_RET

  return 0;
}

/**
 * \brief Estrae un elemento dalla coda senza ottenere il blocco
 * 
 * \param cq La coda da cui estrarre l'elemento
 * \param elem L'elemento estratto
 * \return int 0 in caso di successo, -1 ed errno impostato altrimenti
 */
static int cqueue_pop_nolock(cqueue_t *cq, void **elem) {
  while(cq->size == 0) {
    int ret = pthread_cond_wait(&cq->cnd_avail, &cq->mtx);
    CHECK_RET
  }

  void *res = cq->head->v;

  if(cq->head == cq->tail) {
    /* Caso in cui è presente un solo
      elemento nella coda */
    free(cq->head);
    cq->head = NULL;
    cq->tail = NULL;
    cq->size = 0;
  } else {
    /* Caso standard - la coda
      ha più di un elemento */
    node_t *next = cq->head->next;
    free(cq->head);
    cq->head = next;
    cq->size--;
  }

  *elem = res;

  return 0;
}

int cqueue_pop(cqueue_t *cq, void **elem) {
  if(elem == NULL) {
    errno = EINVAL;
    return -1;
  }

  int ret = pthread_mutex_lock(&cq->mtx);
  CHECK_RET

  ret = cqueue_pop_nolock(cq, elem);
  CHECK_RET

  ret = pthread_mutex_unlock(&cq->mtx);
  CHECK_RET

  return 0;
}

int cqueue_size(cqueue_t *cq) {
  if(cq == NULL) {
    errno = EINVAL;
    return -1;
  }

  int ret = pthread_mutex_lock(&(cq->mtx));
  CHECK_RET

  int size = cq->size;

  ret = pthread_mutex_unlock(&(cq->mtx));
  CHECK_RET

  return size;
}

int cqueue_clear(cqueue_t *cq, cqueue_deinitializer cb) {
  if(cq == NULL) {
    errno = EINVAL;
    return -1;
  }

  int ret = pthread_mutex_lock(&(cq->mtx));
  CHECK_RET

  freeList(cq->head, cb);
  cq->head = NULL;
  cq->tail = NULL;
  cq->size = 0;

  ret = pthread_mutex_unlock(&(cq->mtx));
  CHECK_RET

  return 0;
}