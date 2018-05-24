/**
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

struct ccircbuf {
  size_t len, ptr, num;
  void **elems;
  pthread_mutex_t mtx;
};

ccircbuf_t *ccircbuf_init(size_t len) {
  ccircbuf_t *buf = malloc(sizeof(ccircbuf_t));
  if(buf == NULL) {
    return NULL;
  }

  memset(buf, 0, sizeof(ccircbuf_t));
  buf->len = len;
  buf->elems = malloc(sizeof(void*) * len);

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

  *dest = malloc(sizeof(void*) * buf->len);
  if(*dest == NULL) {
    return -1;
  }

  for(size_t i = 0; i < buf->num; i++) {
    void *elem = buf->elems[(i + buf->ptr) % buf->len];
    *dest[i] = elem;
  }

  return buf->len;
}

int ccircbuf_unlock_elems(ccircbuf_t* buf, void ***elems) {
  if(buf == NULL || elems == NULL) {
    errno = EINVAL;
    return -1;
  }

  free(*elems);
  int ret = pthread_mutex_unlock(&(buf->mtx));
  CHECK_RET

  return 0;
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