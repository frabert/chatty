/**
 *  \author Francesco Bertolaccini 543981
 * 
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "errman.h"
#include "connections.h"
#include "chatty_handlers.h"

#define HANDLER(x) void x(long fd, message_t *msg, payload_t *pl)

HANDLER(handle_register);
HANDLER(handle_connect);
HANDLER(handle_post_txt);
HANDLER(handle_post_txt_all);
HANDLER(handle_post_file);
HANDLER(handle_get_file);
HANDLER(handle_get_prev_msgs);
HANDLER(handle_usr_list);
HANDLER(handle_unregister);
HANDLER(handle_disconnect);
HANDLER(handle_create_group);
HANDLER(handle_add_group);
HANDLER(handle_del_group);

void makeErrorMessage(message_t *msg, op_t error, const char *receiver) {
  memset(msg, 0, sizeof(message_t));
  msg->hdr.op = error;
  if(receiver != NULL)
    memcpy(&(msg->data.hdr.receiver), receiver, strlen(receiver));
}

/**
 * \brief Viene utilizzata da \ref disconnect_client per disassociare un socket
 *        da un nome utente
 * 
 * \param key Il nome utente da disassociare
 * \param value Il descrittore da disassociare
 * \param ud Contesto di lavoro
 */
static void disconnect_client_cb(const char *key, void *value, void *ud) {
  assert(value != NULL);

  client_descriptor_t *cd = (client_descriptor_t*)value;
  cd->fd = -1;
}

void disconnect_client(long fd, payload_t *pl) {
  HANDLE_FATAL(pthread_mutex_lock(&(pl->stats_mtx)), "pthread_mutex_lock");
  pl->chatty_stats.nonline--;
  HANDLE_FATAL(pthread_mutex_unlock(&(pl->stats_mtx)), "pthread_mutex_unlock");
  
  HANDLE_FATAL(pthread_mutex_lock(&(pl->connected_clients_mtx)), "pthread_mutex_lock");
  for(int i = 0; i < pl->cfg->maxConnections; i++) {
    connected_client_t client = pl->connected_clients[i];
    if(client.fd == fd) {
      client.fd = -1;
      chash_get(pl->registered_clients, client.nick, disconnect_client_cb, pl);
      break;
    }
  }
  HANDLE_FATAL(pthread_mutex_unlock(&(pl->connected_clients_mtx)), "pthread_mutex_unlock");

  FD_CLR(fd, &(pl->set));
  close(fd);
}

/**
 * \brief Invia la lista degli utenti registrati
 * 
 * \param fd Il client a cui inviare la lista
 * \param pl Dati di contesto
 */
static void send_user_list(int fd, payload_t *pl) {
  HANDLE_FATAL(pthread_mutex_lock(&(pl->stats_mtx)), "pthread_mutex_lock");
  HANDLE_FATAL(pthread_mutex_lock(&(pl->connected_clients_mtx)), "pthread_mutex_lock");
  unsigned long num = pl->chatty_stats.nusers;
  char *buf = calloc(sizeof(char) * (MAX_NAME_LENGTH + 1), num);
  int c = 0;
  for(int i = 0; i < pl->cfg->maxConnections; i++) {
    if(pl->connected_clients[i].fd > 0) {
      strncpy(buf + (MAX_NAME_LENGTH + 1) * c, pl->connected_clients[i].nick, MAX_NAME_LENGTH);
      c++;
    }
  }

  HANDLE_FATAL(pthread_mutex_unlock(&(pl->connected_clients_mtx)), "pthread_mutex_unlock");
  HANDLE_FATAL(pthread_mutex_unlock(&(pl->stats_mtx)), "pthread_mutex_unlock");
  
  message_t msg;
  memset(&msg, 0, sizeof(message_t));
  msg.hdr.op = OP_OK;
  msg.data.hdr.len = sizeof(char) * (MAX_NAME_LENGTH + 1) * num;
  msg.data.buf = buf;

  int ret = sendRequest(fd, &msg);
  HANDLE_FATAL(ret, "sendRequest");

  if(ret == 0) {
    disconnect_client(fd, pl);
  }

  free(buf);
}

/**
 * \brief Registra il nickname \ref nick
 * 
 * \param msg Il messaggio ricevuto
 * \param fd Il descrittore a cui inviare il messaggio d'errore
 *           nel caso in cui il nickname fosse già registrato
 * \param pl Informazioni di contesto
 */
void handle_register(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == REGISTER_OP);

  client_descriptor_t *cd = calloc(1, sizeof(client_descriptor_t));
  cd->message_buffer = ccircbuf_init(pl->cfg->maxHistMsgs);
  HANDLE_NULL(cd->message_buffer, "ccircbuf_init");

  cd->fd = -1;

  int res = chash_set_if_empty(pl->registered_clients, msg->hdr.sender, cd);
  HANDLE_FATAL(res, "chash_set_if_empty");

  if(res != 0) {
    /* Il nickname era già registrato */
    message_t errMsg;
    makeErrorMessage(&errMsg, OP_NICK_ALREADY, NULL);

    res = sendRequest(fd, &errMsg);
    HANDLE_FATAL(res, "sendRequest");

    if(res == 0) {
      disconnect_client(fd, pl);
    }

    free_client_descriptor(cd);
  } else {
    HANDLE_FATAL(pthread_mutex_lock(&(pl->stats_mtx)), "pthread_mutex_lock");
    pl->chatty_stats.nusers++;
    HANDLE_FATAL(pthread_mutex_unlock(&(pl->stats_mtx)), "pthread_mutex_unlock");
    send_user_list(fd, pl);
  }
}

struct connect_client_data {
  int fd;
  payload_t *pl;
};

static void connect_client_cb(const char *key, void *value, void *ud) {
  struct connect_client_data *data = (struct connect_client_data*)ud;

  int fd = data->fd;
  if(value == NULL) {
    message_t errMsg;
    makeErrorMessage(&errMsg, OP_NICK_UNKNOWN, NULL);

    int ret = sendRequest(fd, &errMsg);
    HANDLE_FATAL(ret, "sendRequest");

    if(ret == 0) {
      /* Il client si è disconnesso */
      disconnect_client(fd, data->pl);
    }
  } else {
    /* Connette il client */
    client_descriptor_t *cd = (client_descriptor_t *)value;
    cd->fd = fd;
    for(int i = 0; i < data->pl->cfg->maxConnections; i++) {
      connected_client_t client = data->pl->connected_clients[i];
      if(client.fd < 0) {
        client.fd = fd;
        memset(client.nick, 0, MAX_NAME_LENGTH + 1);
        strcpy(client.nick, key);
        break;
      }
    }

    send_user_list(fd, data->pl);
  }
}

/**
 * \brief Gestisce una richiesta di connessione da parte di un client
 * 
 * \param msg Il messaggio inviato dal client
 * \param fd Il descrittore associato al client
 * \param pl Dati di contesto
 */
void handle_connect(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == CONNECT_OP);

  struct connect_client_data data;
  data.pl = pl;
  data.fd = fd;

  chash_get(pl->registered_clients, msg->hdr.sender, connect_client_cb, &data);
}

void handle_post_txt(long fd, message_t *msg, payload_t *pl) {}
void handle_post_txt_all(long fd, message_t *msg, payload_t *pl) {}
void handle_post_file(long fd, message_t *msg, payload_t *pl) {}
void handle_get_file(long fd, message_t *msg, payload_t *pl) {}
void handle_get_prev_msgs(long fd, message_t *msg, payload_t *pl) {}
void handle_usr_list(long fd, message_t *msg, payload_t *pl) {}
void handle_unregister(long fd, message_t *msg, payload_t *pl) {}
void handle_disconnect(long fd, message_t *msg, payload_t *pl) {}
void handle_create_group(long fd, message_t *msg, payload_t *pl) {}
void handle_add_group(long fd, message_t *msg, payload_t *pl) {}
void handle_del_group(long fd, message_t *msg, payload_t *pl) {}

chatty_request_handler *chatty_handlers[] = {
  &handle_register,
  &handle_connect,
  &handle_post_txt,
  &handle_post_txt_all,
  &handle_post_file,
  &handle_get_file,
  &handle_get_prev_msgs,
  &handle_usr_list,
  &handle_unregister,
  &handle_disconnect,
  &handle_create_group,
  &handle_add_group,
  &handle_del_group
};