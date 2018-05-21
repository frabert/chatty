/** \file errman.h
 *  \author Francesco Bertolaccini 543981
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include "connections.h"

static ssize_t readn(long fd, void *buf, size_t len) {
  size_t offset = 0;
  while(offset < len) {
    ssize_t r = read(fd, buf + offset, len - offset);
    while(r < 0 && errno == EINTR) {
      r = read(fd, buf + offset, len - offset);
    }
    if(r == 0) return 0;
    if(r < 0) return -1;

    offset += r;
  }
  return 1;
}

static ssize_t writen(long fd, void *buf, size_t len) {
  size_t offset = 0;
  while(offset < len) {
    ssize_t w = write(fd, buf + offset, len - offset);
    while(w < 0 && errno == EINTR) {
      w = write(fd, buf + offset, len - offset);
    }
    if(w == 0) return 0;
    if(w < 0) return -1;

    offset += w;
  }
  return 1;
}

int readHeader(long fd, message_hdr_t *hdr) {
  return readn(fd, (void*)hdr, sizeof(message_hdr_t));
}

int readDataHeader(long fd, message_data_hdr_t *datahdr) {
  return readn(fd, (void*)datahdr, sizeof(message_data_hdr_t));
}

int readData(long fd, message_data_t *data) {
  int res;
  if((res = readDataHeader(fd, &(data->hdr))) > 0) {
    void *buf = malloc(data->hdr->len);
    if((res = readn(fd, buf, data->hdr->len)) > 0) {
      data->buf = buf;
    }
    return res;
  } else {
    return res;
  }
}

int readMsg(long fd, message_t *msg) {
  int res;
  if((res = readHeader(fd, &(msg->hdr))) > 0 &&
     (res = readData(fd, &(msg->data))) > 0) {
    return res;
  }
  return res;
}

int sendRequest(long fd, message_t *msg) {
  
}