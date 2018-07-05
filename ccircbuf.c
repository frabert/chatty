/**
 *  \file ccircbuf.c
 *  \author Francesco Bertolaccini 543981
 * 
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 * 
 */

#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "ccircbuf.h"

#define CHECK_RET if(ret != 0) { \
                    errno = ret; \
                    return -1; \
                  }

/**
 * \brief Buffer circolare concorrente 
 */
struct ccircbuf {
  size_t len; ///< Capienza del buffer
  size_t ptr; ///< Testa del buffer
  size_t num; ///< Numero di elementi attualmente contenuti nel buffer
  void **elems; ///< Contenuto del buffer
  pthread_mutex_t mtx; ///< Mutex per l'accesso al buffer
};

ccircbuf_t *ccircbuf_init(size_t len) {
  ccircbuf_t *buf = calloc(1, sizeof(ccircbuf_t));
  if(buf == NULL) {
    return NULL;
  }

  buf->len = len;
  buf->elems = calloc(sizeof(void*), len);

  if(buf->elems == NULL) {
    return NULL;
  }

  int ret = pthread_mutex_init(&(buf->mtx), NULL);
  if(ret != 0) {
    errno = ret;
    return NULL;
  }

  return buf;
}

int ccircbuf_deinit(ccircbuf_t *buf) {
  if(buf == NULL) {
    errno = EINVAL;
    return -1;
  }

  free(buf->elems);

  int ret = pthread_mutex_destroy(&(buf->mtx));
  CHECK_RET
  free(buf);

  return 0;
}

int ccircbuf_get_elems(ccircbuf_t* buf, void ***dest) {
  if(buf == NULL || dest == NULL) {
    errno = EINVAL;
    return -1;
  }

  int ret = pthread_mutex_lock(&(buf->mtx));
  CHECK_RET

  size_t len = buf->num;
  *dest = calloc(sizeof(void*), len);
  if(*dest == NULL) {
    return -1;
  }

  for(size_t i = 0; i < buf->num; i++) {
    void *elem = buf->elems[(i + buf->ptr) % len];
    (*dest)[i] = elem;
  }
  
  ret = pthread_mutex_unlock(&(buf->mtx));
  CHECK_RET

  return len;
}

int ccircbuf_insert(ccircbuf_t* buf, void *elem, void **oldElem) {
  if(buf == NULL) {
    errno = EINVAL;
    return -1;
  }

  int ret = pthread_mutex_lock(&(buf->mtx));
  CHECK_RET

  if(buf->num < buf->len) {
    buf->elems[buf->num] = elem;
    if(oldElem != NULL) *oldElem = NULL;
    buf->num++;
  } else {
    if(oldElem != NULL) *oldElem = buf->elems[buf->ptr];
    buf->elems[buf->ptr] = elem;
    buf->ptr = (buf->ptr + 1) % buf->len;
  }

  ret = pthread_mutex_unlock(&(buf->mtx));
  CHECK_RET

  return 0;
}