/**
 *  \author Francesco Bertolaccini 543981
 * 
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
#include <stdlib.h>
#include <stdint.h>
#include "connections.h"

/**
 * \brief Legge esattamente len byte dal descrittore fd
 * 
 * \param fd Il descrittore da cui leggere
 * \param buf Buffer in cui mettere i dati letti
 * \param len Numero di byte da leggere
 * \return ssize_t 1 in caso di successo, -1 in caso di errore,
 *                 0 se il descrittore è stato chiuso prima della lettura completa
 */
static ssize_t readn(long fd, void *buf, size_t len) {
  if(buf == NULL) {
    return 1;
  }

  size_t offset = 0;
  uint8_t *bytes = (uint8_t*)buf;

  while(offset < len) {
    ssize_t r = read(fd, bytes + offset, len - offset);
    while(r < 0 && errno == EINTR) {
      r = read(fd, bytes + offset, len - offset);
    }
    if(r == 0) return 0;
    if(r < 0) return -1;

    offset += r;
  }
  return 1;
}

/**
 * \brief Scrite esattamente len byte sul descrittore fd
 * 
 * \param fd Il descrittore su cui scrivere
 * \param buf Buffer contenente i dati da scrivere
 * \param len Numero di byte da scrivere
 * \return ssize_t 1 in caso di successo, -1 in caso di errore,
 *                 0 se il descrittore è stato chiuso prima della scrittura completa
 */
static ssize_t writen(long fd, void *buf, size_t len) {
  if(buf == NULL) {
    return 1;
  }

  size_t offset = 0;
  uint8_t *bytes = (uint8_t*)buf;
  while(offset < len) {
    ssize_t w = write(fd, bytes + offset, len - offset);
    while(w < 0 && errno == EINTR) {
      w = write(fd, bytes + offset, len - offset);
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
    void *buf = malloc(data->hdr.len);
    if(buf == NULL) {
      return -1;
    }

    if((res = readn(fd, buf, data->hdr.len)) > 0) {
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

/**
 * \brief Invia un header dati ad un descrittore
 * 
 * \param fd Il descrittore a cui inviare l'header
 * \param datahdr L'header da inviare
 * \return int Vedi writen
 */
static int sendDataHeader(long fd, message_data_hdr_t *datahdr) {
  return writen(fd, (void*)datahdr, sizeof(message_data_hdr_t));
}

/**
 * \brief Invia un header di messaggio ad un descrittore
 * 
 * \param fd Il descrittore a cui inviare l'header
 * \param hdr L'header da inviare
 * \return int Vedi writen
 */
static int sendHeader(long fd, message_hdr_t *hdr) {
  return writen(fd, (void*)hdr, sizeof(message_hdr_t));
}

int sendData(long fd, message_data_t *data) {
  int res = sendDataHeader(fd, &(data->hdr));
  if(res > 0) {
    return writen(fd, data->buf, data->hdr.len);
  } else {
    return res;
  }
}

int sendRequest(long fd, message_t *msg) {
  int res = sendHeader(fd, &(msg->hdr));
  if(res > 0) {
    return sendData(fd, &(msg->data));
  } else {
    return res;
  }
}

int openConnection(char* path, unsigned int ntimes, unsigned int secs) {
  int fd_skt;
  struct sockaddr_un sa;
  strcpy(sa.sun_path, path);
  sa.sun_family = AF_UNIX;
  fd_skt = socket(AF_UNIX,SOCK_STREAM,0);
  unsigned int tries = 0;
  while (connect(fd_skt, (struct sockaddr*)&sa, sizeof(sa)) == -1) 
  {
    if(tries < ntimes) tries++;
    else return -1;
    sleep(secs); 
  }
  return fd_skt;
}