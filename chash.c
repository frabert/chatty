/**
 *  \file chash.c
 *  \author Francesco Bertolaccini 543981
 * 
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 */

#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "chash.h"

/// La dimensione di una hashtable
#define NUM_HASH_ENTRIES 1024

#define NUM_HASH_CLUSTERS 64

#define HASH_CLUSTER_SIZE ((NUM_HASH_ENTRIES)/(NUM_HASH_CLUSTERS))

#define CHECK_RET if(ret != 0) { \
                    errno = ret; \
                    return -1; \
                  }

/**
 * \brief Una lista di trabocco di una tabella hash
 */
typedef struct chash_entry {
  char *key; ///< Chiave del nodo
  void *value; ///< Valore del nodo
  struct chash_entry *next; ///< Nodo successivo
} chash_entry_t;

/**
 * \brief Tabella hash concorrente
 */
struct chash {
  chash_entry_t *entries[NUM_HASH_ENTRIES]; ///< Tabella dei nodi
  int numkeys; ///< Numero di nodi attualmente contenuti
  pthread_mutex_t mutexes[NUM_HASH_CLUSTERS]; ///< Mutexes per l'accesso ai cluster
};

/* Da http://www.cse.yorku.ca/~oz/hash.html */
static size_t hash(const char *str) {
  size_t hash = 5381;
  size_t c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

  return (size_t)(hash % NUM_HASH_ENTRIES);
}

chash_t *chash_init() {
  chash_t *ht = calloc(1, sizeof(chash_t));
  if(ht == NULL) return NULL;

  memset(ht, 0, sizeof(chash_t));
  for(int i = 0; i < NUM_HASH_CLUSTERS; i++) {
    int ret = pthread_mutex_init(&(ht->mutexes[i]), NULL);
    if(ret != 0) {
      errno = ret;
      return NULL;
    }
  }

  return ht;
}

int chash_get(chash_t *ht, const char *key, chash_get_callback cb, void *ud) {
  if(cb == NULL || ht == NULL) {
    errno = EINVAL;
    return -1;
  }

  size_t idx = hash(key);
  pthread_mutex_t *mtx = &(ht->mutexes[idx / HASH_CLUSTER_SIZE]);
  int ret = pthread_mutex_lock(mtx);

  CHECK_RET

  if(ht->entries[idx] == NULL) {
    ret = pthread_mutex_unlock(mtx);
    CHECK_RET

    cb(key, NULL, ud);
    return 0;
  } else {
    chash_entry_t *ptr = ht->entries[idx];
    while(ptr != NULL && strcmp(ptr->key, key) != 0) {
      ptr = ptr->next;
    }

    if(ptr == NULL) {
      ret = pthread_mutex_unlock(mtx);
      CHECK_RET

      cb(key, NULL, ud);
      return 0;
    } else {
      cb(key, ptr->value, ud);
    }

  }
  ret = pthread_mutex_unlock(mtx);
  CHECK_RET

  return 0;
}

static int chash_lock_all(chash_t *ht) {
  for(int i = 0; i < NUM_HASH_CLUSTERS; i++) {
    int ret = pthread_mutex_lock(&(ht->mutexes[i]));
    CHECK_RET
  }

  return 0;
}

static int chash_unlock_all(chash_t *ht) {
  for(int i = 0; i < NUM_HASH_CLUSTERS; i++) {
    int ret = pthread_mutex_unlock(&(ht->mutexes[i]));
    CHECK_RET
  }

  return 0;
}

int chash_get_all(chash_t *ht, chash_get_callback cb, void *ud) {
  if(cb == NULL || ht == NULL) {
    errno = EINVAL;
    return -1;
  }

  int ret = chash_lock_all(ht);
  CHECK_RET

  for(size_t i = 0; i < NUM_HASH_ENTRIES; i++) {
    if(ht->entries[i] != NULL) {
      chash_entry_t *ptr = ht->entries[i];
      while(ptr != NULL) {
        cb(ptr->key, ptr->value, ud);
        ptr = ptr->next;
      }
    }
  }

  ret = chash_unlock_all(ht);
  CHECK_RET
  return 0;
}

int chash_set(chash_t *table, const char *key, void *value, void **oldValue) {
  if(table == NULL) {
    errno = EINVAL;
    return -1;
  }

  size_t idx = hash(key);
  size_t keyLen = strlen(key);
  pthread_mutex_t *mtx = &(table->mutexes[idx / HASH_CLUSTER_SIZE]);
  int ret = pthread_mutex_lock(mtx);
  CHECK_RET
  if(table->entries[idx] == NULL) {
    if(value == NULL) {
      goto end;
    }
    chash_entry_t *newEntry = calloc(1, sizeof(chash_entry_t));
    if(newEntry == NULL) {
      ret = pthread_mutex_unlock(mtx);
      if(ret != 0) {
        errno = ret;
      }
      return -1;
    }

    newEntry->key = calloc(keyLen + 1, sizeof(char));
    if(newEntry->key == NULL) {
      ret = pthread_mutex_unlock(mtx);
      if(ret != 0) {
        errno = ret;
      }
      return -1;
    }

    strncpy(newEntry->key, key, keyLen);
    newEntry->value = value;
    newEntry->next = NULL;

    table->entries[idx] = newEntry;
    table->numkeys++;

    if(oldValue != NULL) *oldValue = NULL;

  } else {
    chash_entry_t  *prev = NULL;
    chash_entry_t *ptr = table->entries[idx];
    while(ptr != NULL && strcmp(ptr->key, key) != 0) {
      prev = ptr;
      ptr = ptr->next;
    }

    if(ptr == NULL) {
      if(value == NULL) {
        goto end;
      }
      
      chash_entry_t *newEntry = malloc(sizeof(chash_entry_t));
      if(newEntry == NULL) {
        ret = pthread_mutex_unlock(mtx);
        CHECK_RET
        return -1;
      }

      newEntry->key = calloc(keyLen + 1, sizeof(char));
      if(newEntry->key == NULL) {
        ret = pthread_mutex_unlock(mtx);
        CHECK_RET
        return -1;
      }
      
      strncpy(newEntry->key, key, keyLen);
      newEntry->value = value;
      newEntry->next = table->entries[idx];

      table->entries[idx] = newEntry;
      table->numkeys++;

      if(oldValue != NULL) *oldValue = NULL;
    } else {
      if(oldValue != NULL) *oldValue = ptr->value;
      if(value == NULL) {
        if(prev == NULL) {
          table->entries[idx] = ptr->next;
        } else {
          prev->next = ptr->next;
        }
        free(ptr->key);
        free(ptr);
        table->numkeys--;
      } else {
        ptr->value = value;
      }
    }
  }

end:
  ret = pthread_mutex_unlock(mtx);
  CHECK_RET
  return 0;
}

int chash_set_if_empty(chash_t *table, const char *key, void *value) {
  if(table == NULL) {
    errno = EINVAL;
    return -1;
  }

  size_t idx = hash(key);
  size_t keyLen = strlen(key);
  pthread_mutex_t *mtx = &(table->mutexes[idx / HASH_CLUSTER_SIZE]);
  int ret = pthread_mutex_lock(mtx);
  CHECK_RET
  if(table->entries[idx] == NULL) {
    if(value == NULL) {
      goto end;
    }
    chash_entry_t *newEntry = calloc(1, sizeof(chash_entry_t));
    if(newEntry == NULL) {
      ret = pthread_mutex_unlock(mtx);
      if(ret != 0) {
        errno = ret;
      }
      return -1;
    }

    newEntry->key = calloc(keyLen + 1, sizeof(char));
    if(newEntry->key == NULL) {
      ret = pthread_mutex_unlock(mtx);
      if(ret != 0) {
        errno = ret;
      }
      return -1;
    }

    strncpy(newEntry->key, key, keyLen);
    newEntry->value = value;
    newEntry->next = NULL;

    table->entries[idx] = newEntry;
    table->numkeys++;
  } else {
    chash_entry_t *ptr = table->entries[idx];
    while(ptr != NULL && strcmp(ptr->key, key) != 0) {
      ptr = ptr->next;
    }

    if(ptr == NULL) {
      if(value == NULL) {
        goto end;
      }
      
      chash_entry_t *newEntry = calloc(1, sizeof(chash_entry_t));
      if(newEntry == NULL) {
        ret = pthread_mutex_unlock(mtx);
        CHECK_RET
        return -1;
      }

      newEntry->key = calloc(keyLen + 1, sizeof(char));
      if(newEntry->key == NULL) {
        ret = pthread_mutex_unlock(mtx);
        CHECK_RET
        return -1;
      }
      strncpy(newEntry->key, key, keyLen);

      newEntry->value = value;
      newEntry->next = table->entries[idx];
      
      table->entries[idx] = newEntry;
      table->numkeys++;
    } else {
      ret = pthread_mutex_unlock(mtx);
      CHECK_RET
      return 1;
    }
  }

end:
  ret = pthread_mutex_unlock(mtx);
  CHECK_RET
  return 0;
}

static int deinit_elem(chash_entry_t *elem, chash_deinitializer cb) {
  if(elem->next != NULL) {
    deinit_elem(elem->next, cb);
  }

  free(elem->key);
  cb(elem->value);
  free(elem);
  return 0;
}

int chash_deinit(chash_t *table, chash_deinitializer cb) {
  if(table == NULL) {
    errno = EINVAL;
    return -1;
  }

  for(size_t i = 0; i < NUM_HASH_ENTRIES; i++) {
    if(table->entries[i] != NULL) {
      if(deinit_elem(table->entries[i], cb) != 0) {
        return -1;
      }
    }
  }
  for(int i = 0; i < NUM_HASH_CLUSTERS; i++) {
    int ret = pthread_mutex_destroy(&(table->mutexes[i]));
    if(ret != 0) {
      errno = ret;
      free(table);
      return -1;
    }
  }
  
  
  free(table);
  return 0;
}

int chash_keys(chash_t *ht, char ***keys) {
  if(ht == NULL || keys == NULL) {
    errno = EINVAL;
    return -1;
  }

  int ret = chash_lock_all(ht);
  CHECK_RET

  int numkeys = ht->numkeys;

  *keys = calloc(numkeys, sizeof(char **));
  if(*keys == NULL) {
    return -1;
  }
  int j = 0;
  char **arr = *keys;

  for(size_t i = 0; i < NUM_HASH_ENTRIES; i++) {
    if(ht->entries[i] != NULL) {
      chash_entry_t *ptr = ht->entries[i];
      while(ptr != NULL) {
        size_t len = strlen(ptr->key);
        arr[j] = calloc(len + 1, sizeof(char));
        strncpy(arr[j], ptr->key, len);
        j++;

        ptr = ptr->next;
      }
    }
  }

  ret = chash_unlock_all(ht);
  CHECK_RET
  return numkeys;
}