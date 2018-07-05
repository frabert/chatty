#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include "ccircbuf.h"

struct arg {
  int val;
  ccircbuf_t *buf;
};

void *func(void *ud) {
  struct arg *a = (struct arg*)ud;

  void *oldVal;
  ccircbuf_insert(a->buf, (void*)a->val, &oldVal);
  return NULL;
}

int main(void) {
  ccircbuf_t *buf = ccircbuf_init(16);
  pthread_t threads[8];
  struct arg args[8];

  for(int i = 0; i < 8; i++) {
    args[i].val = i;
    args[i].buf = buf;
    pthread_create(threads + i, NULL, func, args + i);
  }

  for(int i = 0; i < 8; i++) {
    pthread_join(threads[i], NULL);
  }
  
  void **elems;
  int num = ccircbuf_get_elems(buf, &elems);
  assert(num == 8);
  for(int i = 0; i < 8; i++) {
    assert((int)elems[i] < 8);
  }
  free(elems);

  return 0;
}