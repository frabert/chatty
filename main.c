#include <stdio.h>
#include <stdlib.h>
#include "cfgparse.h"

int callback(const char* name, const char* val, void *ud) {
  printf("%s = %s\n", name, val);
  return 1;
}

void main() {
    FILE *f = fopen("DATA/chatty.conf1", "r");
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);  //same as rewind(f);

  char *string = malloc(fsize + 1);
  fread(string, fsize, 1, f);
  fclose(f);

  string[fsize] = 0;

  int res = cfg_parse(string, callback, NULL);
  free(string);
}