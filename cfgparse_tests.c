#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "cfgparse.h"

static const char *test = "# this is a comment\n"
                          "a = 1\n"
                          "foo =     bar  # foobar  \n\n"
                          "hello = world!\n"
                          "ciao = \"come stai?\"";

int cb(const char *key, const char *value, void *ud) {
  if(strcmp(key, "a") == 0) {
    assert(strcmp(value, "1") == 0);
  } else if(strcmp(key, "foo") == 0) {
    assert(strcmp(value, "bar") == 0);
  } else if(strcmp(key, "hello") == 0) {
    assert(strcmp(value, "world!") == 0);
  } else if(strcmp(key, "ciao") == 0) {
    assert(strcmp(value, "come stai?") == 0);
  } else {
    assert(0);
  }

  return 0;
}

int main(void) {
  cfg_parse(test, cb, NULL);
  return 0;
}