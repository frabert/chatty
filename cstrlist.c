/**
 *  \author Francesco Bertolaccini 543981
 * 
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 */

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include "cstrlist.h"

#define CHECK_RET if(ret != 0) { \
                    errno = ret; \
                    return -1; \
                  }

/**
 * \brief Nodo di una lista linkata doppia
 */
typedef struct node {
  char *v; ///< Valore del nodo
  struct node *prev; ///< Nodo precedente
  struct node *next; ///< Nodo successivo
} node_t;

/**
 * \brief Lista di stringhe concorrente
 */
struct cstrlist {
  node_t *head; ///< Primo elemento della lista
  size_t size; ///< Lunghezza della lista
  pthread_rwlock_t lock; ///< Lock R/W per l'accesso ai valori
};

cstrlist_t *cstrlist_init() {
  cstrlist_t *list = calloc(1, sizeof(cstrlist_t));
  if(!list) return NULL;
  int ret;
  if((ret = pthread_rwlock_init(&list->lock, NULL)) != 0) {
    goto fail;
  }

  return list;

fail:
  errno = ret;
  free(list);
  return NULL;
}

static void freeList(node_t *node) {
  if(node == NULL) return;

  if(node->next != NULL) freeList(node->next);

  free(node->v);
  free(node);
}

int cstrlist_deinit(cstrlist_t *list) {
  if(list == NULL) {
    errno = EINVAL;
    return -1;
  }

  freeList(list->head);
  
  int ret = 0;
  if((ret = pthread_rwlock_destroy(&list->lock)) != 0) {
    errno = ret;
    ret = -1;
  }

  free(list);
  return ret;
}

int cstrlist_insert(cstrlist_t *list, const char *v) {
  if(list == NULL || v == NULL) {
    errno = EINVAL;
    return -1;
  }

  size_t len = strlen(v);

  node_t *newNode = calloc(1, sizeof(node_t));
  if(!newNode) return -1;

  newNode->v = calloc(len + 1, sizeof(char));
  if(!newNode->v) return -1;

  strncpy(newNode->v, v, len);

  int ret = pthread_rwlock_wrlock(&(list->lock));
  CHECK_RET

  if(list->head == NULL) {
    list->head = newNode;
  } else {
    node_t *cur = list->head;
    node_t *prev = NULL;
    while(cur != NULL) {
      if(strcmp(v, cur->v) == 0) {
        errno = EALREADY;
        goto fail;
      }
      prev = cur;
      cur = cur->next;
    }
    prev->next = newNode;
    newNode->prev = prev;
  }
  
  list->size++;

  ret = pthread_rwlock_unlock(&(list->lock));
  CHECK_RET

  return 0;

fail:
  ret = pthread_rwlock_unlock(&(list->lock));
  CHECK_RET
  return -1;
}

int cstrlist_remove(cstrlist_t *list, const char *v) {
  if(list == NULL || v == NULL) {
    errno = EINVAL;
    return -1;
  }

  int ret = pthread_rwlock_wrlock(&(list->lock));
  CHECK_RET

  int found = 0;
  if(list->head != NULL) {
    node_t *cur = list->head;
    while(cur != NULL) {
      if(strcmp(v, cur->v) == 0) {
        found = 1;
        node_t *prev = cur->prev;
        node_t *next = cur->next;

        if(prev == NULL) {
          list->head = next;
        } else {
          prev->next = next;
        }

        if(next != NULL) {
          next->prev = prev;
        }

        free(cur->v);
        free(cur);
        break;
      }
      cur = cur->next;
    }
  }

  if(found) {
    list->size--;
  } else {
    errno = ENOENT;
    goto fail;
  }

  ret = pthread_rwlock_unlock(&(list->lock));
  CHECK_RET
  return 0;

fail:
  ret = pthread_rwlock_unlock(&(list->lock));
  CHECK_RET
  return -1;
}

int cstrlist_get_values(cstrlist_t *list, char ***dest) {
  if(list == NULL || dest == NULL) {
    errno = EINVAL;
    return -1;
  }

  int ret = pthread_rwlock_rdlock(&(list->lock));
  CHECK_RET

  char **buf = calloc(list->size, sizeof(char*));
  if(!buf) goto fail;

  node_t *cur = list->head;

  for(size_t i = 0; i < list->size; i++) {
    buf[i] = strdup(cur->v);
    if(!buf[i]) goto fail;
    cur = cur->next;
  }

  *dest = buf;

  ret = pthread_rwlock_unlock(&(list->lock));
  CHECK_RET
  return list->size;

fail:
  ret = pthread_rwlock_unlock(&(list->lock));
  CHECK_RET
  return -1;
}