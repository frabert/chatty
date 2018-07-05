#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include "chash.h"

struct arg {
  const char *key;
  int val;
  chash_t *ht;
};

void *func_1(void *ud) {
  struct arg *a = (struct arg*)ud;

  void *oldVal;
  chash_set(a->ht, a->key, (void*)a->val, &oldVal);
  return NULL;
}

void func_2(const char *key, void *val, void *ud) {
  const char **keys = (const char**)ud;
  for(int i = 0; i < 8; i++) {
    if(strcmp(key, keys[i]) == 0) {
      assert((int)val == i);
      return;
    }
  }
  assert(0);
}

int main(void) {
  chash_t *ht = chash_init();
  pthread_t threads[8];
  struct arg args[8];
  const char *keys[8] = {
    "chiave 1", "chiave 2",
    "chiave 3", "chiave 4",
    "chiave 5", "chiave 6",
    "chiave 7", "chiave 8"
  };

  for(int i = 0; i < 8; i++) {
    args[i].val = i;
    args[i].ht = ht;
    args[i].key = keys[i];
    pthread_create(threads + i, NULL, func_1, args + i);
  }

  for(int i = 0; i < 8; i++) {
    pthread_join(threads[i], NULL);
  }

  chash_get_all(ht, func_2, keys);

  return 0;
}