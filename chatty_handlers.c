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

/// Dichiara un gestore di richieste
#define HANDLER(x) void x(long fd, message_t *msg, payload_t *pl)

/// Aumenta in maniera atomica il numero di errori inviati
#define INCREASE_ERRORS(pl) HANDLE_FATAL(pthread_mutex_lock(&((pl)->stats_mtx)), "pthread_mutex_lock"); \
                            (pl)->chatty_stats.nerrors++; \
                            HANDLE_FATAL(pthread_mutex_unlock(&((pl)->stats_mtx)), "pthread_mutex_unlock");

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

/**
 * \brief Inizializza un messaggio d'errore
 * 
 * \param msg Messaggio da inizializzare
 * \param error Il tipo di errore
 * \param receiver Nick del destinatario
 */
void make_error_message(message_t *msg, op_t error, const char *receiver, const char *text) {
  memset(msg, 0, sizeof(message_t));
  msg->hdr.op = error;
  if(receiver != NULL)
    memcpy(&(msg->data.hdr.receiver), receiver, strlen(receiver));

  if(text != NULL) {
    size_t len = strlen(text);
    msg->data.hdr.len = len;
    msg->data.buf = calloc(len + 1, sizeof(char));
    strncpy(msg->data.buf, text, len);
  }

  return;
}

/**
 * \brief Invia un messaggio ad un client e gestisce automaticamente l'evenutale disconnessione
 * 
 * \param fd Descrittore a cui inviare il messaggio
 * \param msg Il messaggio da inviare
 * \param pl Dati di contesto
 * \return int 0 se il client si è disconnesso, -1 in caso di errore (ed errno impostato), altro altrimenti
 */
static int send_handle_disconnect(long fd, message_t *msg, payload_t *pl) {
  int ret = sendRequest(fd, msg);
  if(ret == 0) {
    disconnect_client(fd, pl);
  }

  return ret;
}

void send_error_message(long fd, op_t error, payload_t *pl, const char *receiver, const char *text) {
  message_t errMsg;
  make_error_message(&errMsg, error, receiver, text);

  int ret = send_handle_disconnect(fd, &errMsg, pl);
  HANDLE_FATAL(ret, "send_handle_disconnect");
  if(ret == 0) {
    disconnect_client(fd, pl);
  }

  if(errMsg.data.buf) free(errMsg.data.buf);
}

/**
 * \brief Viene utilizzata da \ref disconnect_client per disassociare un socket
 *        da un nome utente
 * 
 * \param key Il nome utente da disassociare
 * \param value Il descrittore da disassociare (\ref client_descriptor_t*)
 * \param ud Contesto di lavoro (\ref payload_t*)
 */
static void disconnect_client_cb(const char *key, void *value, void *ud) {
  assert(ud != NULL);

  if(value == NULL) return;

  client_descriptor_t *cd = (client_descriptor_t*)value;
  cd->fd = -1;
}

/**
 * \brief Restituisce il client associato al descrittore \ref fd
 * 
 * \param fd Il descrittore da cercare
 * \param pl Dati di contesto
 * \return connected_client_t* Il client trovato. NULL se \ref fd non si riferisce ad alcun client connesso
 */
static connected_client_t *find_connected_client(long fd, payload_t *pl) {
  for(int i = 0; i < pl->cfg->maxConnections; i++) {
    connected_client_t *client = &(pl->connected_clients[i]);
    if(client->fd == fd) {
      return client;
    }
  }
  return NULL;
}

void disconnect_client(long fd, payload_t *pl) {
  HANDLE_FATAL(pthread_mutex_lock(&(pl->stats_mtx)), "pthread_mutex_lock");
  pl->chatty_stats.nonline--;
  
  HANDLE_FATAL(pthread_mutex_lock(&(pl->connected_clients_mtx)), "pthread_mutex_lock");

  connected_client_t *client = find_connected_client(fd, pl);
  if(client != NULL) {
    client->fd = -1;
    chash_get(pl->registered_clients, client->nick, disconnect_client_cb, pl);
  }
  HANDLE_FATAL(pthread_mutex_unlock(&(pl->connected_clients_mtx)), "pthread_mutex_unlock");
  HANDLE_FATAL(pthread_mutex_unlock(&(pl->stats_mtx)), "pthread_mutex_unlock");

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
#if SEND_ONLINE_USERS
  HANDLE_FATAL(pthread_mutex_lock(&(pl->stats_mtx)), "pthread_mutex_lock");
  HANDLE_FATAL(pthread_mutex_lock(&(pl->connected_clients_mtx)), "pthread_mutex_lock");
  unsigned long num = pl->chatty_stats.nonline;
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
#else
  char **users;
  int num = chash_keys(pl->registered_clients, &users);
  HANDLE_FATAL(num, "chash_keys");

  char *buf = calloc(sizeof(char) * (MAX_NAME_LENGTH + 1), num);
  
  for(int i = 0; i < num; i++) {
    strncpy(buf + (MAX_NAME_LENGTH + 1) * i, users[i], MAX_NAME_LENGTH);
    free(users[i]);
  }
  free(users);
#endif
  
  message_t msg;
  memset(&msg, 0, sizeof(message_t));
  msg.hdr.op = OP_OK;
  msg.data.hdr.len = sizeof(char) * (MAX_NAME_LENGTH + 1) * num;
  msg.data.buf = buf;

  int ret = send_handle_disconnect(fd, &msg, pl);
  HANDLE_FATAL(ret, "send_handle_disconnect");

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
    INCREASE_ERRORS(pl);

    send_error_message(fd, OP_NICK_ALREADY, pl, NULL, "Nickname già registrato");

    free_client_descriptor(cd);
  } else {
    HANDLE_FATAL(pthread_mutex_lock(&(pl->stats_mtx)), "pthread_mutex_lock");
    pl->chatty_stats.nusers++;
    HANDLE_FATAL(pthread_mutex_unlock(&(pl->stats_mtx)), "pthread_mutex_unlock");
    send_user_list(fd, pl);
  }
}

/**
 * \brief Dati passati dalle funzioni di gestione alle callback
 */
struct callback_data {
  int fd; ///< Descrittore del client che ha effettutato la richiesta
  payload_t *pl; ///< Dati di contesto
};

/**
 * \brief Viene utilizzata da \ref handle_connect per disassociare un socket
 *        da un nome utente
 * 
 * \param key Il nome utente da associare
 * \param value Descrittore del client da associare (\ref client_descriptor_t*)
 * \param ud Contesto di lavoro (\ref callback_data*)
 */
static void handle_connect_cb(const char *key, void *value, void *ud) {
  assert(ud != NULL);
  struct callback_data *data = (struct callback_data*)ud;

  int fd = data->fd;
  if(value == NULL) {
    /* Tentativo di connessione ad un nickname non registrato */
    INCREASE_ERRORS(data->pl);

    send_error_message(fd, OP_NICK_UNKNOWN, data->pl, NULL, "Nickname non esistente");
  } else {
    client_descriptor_t *cd = (client_descriptor_t *)value;

    /* Controllo se qualcuno è già connesso con lo stesso nickname */
    if(cd->fd > 0) {
      send_error_message(fd, OP_FAIL, data->pl, NULL, "Nickname già connesso");
      return;
    }

    cd->fd = fd;
    for(int i = 0; i < data->pl->cfg->maxConnections; i++) {
      connected_client_t *client = &(data->pl->connected_clients[i]);
      if(client->fd < 0) {
        client->fd = fd;
        memset(client->nick, 0, MAX_NAME_LENGTH + 1);
        strncpy(client->nick, key, MAX_NAME_LENGTH);

        send_user_list(fd, data->pl);
        return;
      }
    }

    /* Questo punto non dovrebbe essere mai raggiunto, perchè il controllo
       sul numero di slot liberi viene effettuato prima */
    assert(0);
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

  struct callback_data data;
  data.pl = pl;
  data.fd = fd;

  int ret = chash_get(pl->registered_clients, msg->hdr.sender, handle_connect_cb, &data);
  HANDLE_FATAL(ret, "chash_get");
}

/**
 * \brief Instrada un messaggio verso un client
 * 
 * \param key Il nome utente del client verso cui instradare il messaggio
 * \param value Puntatore al descrittore del client verso cui instradare
 * \param ud Puntatore al pacchetto da instradare
 */
static void route_message_to_client(const char *key, void *value, void *ud) {
  client_descriptor_t *client = (client_descriptor_t*)value;
  message_packet_t *pkt = (message_packet_t*)ud;

  if(client == NULL) {
    /* Il messaggio è stato instradato verso un utente non esistente */
    INCREASE_ERRORS(pkt->pl);
    
    send_error_message(pkt->fd, OP_NICK_UNKNOWN, pkt->pl, key, "Nickname non esistente");
  } else {
    message_t *msg = calloc(1, sizeof(message_t));
    memcpy(msg, &(pkt->message), sizeof(message_t));

    message_t *oldMsg;

    int ret = ccircbuf_insert(client->message_buffer, msg, (void*)&oldMsg);
    HANDLE_FATAL(ret, "ccircbuf_insert");
    free(oldMsg);
    if(client->fd > 0) {
      /* Il client è connesso, gli invio il messaggio */
      message_t newMsg;
      memcpy(&newMsg, &(pkt->message), sizeof(message_t));

      if(newMsg.hdr.op == POSTTXT_OP || newMsg.hdr.op == POSTTXTALL_OP) {
        newMsg.hdr.op = TXT_MESSAGE;
      } else {
        newMsg.hdr.op = FILE_MESSAGE;
        memcpy(newMsg.data.hdr.receiver, key, MAX_NAME_LENGTH + 1);
      }

      ret = send_handle_disconnect(client->fd, &newMsg, pkt->pl);
      HANDLE_FATAL(ret, "send_handle_disconnect");
    }

    if(!(pkt->broadcast)) {
      /* Se inviassi un ack qua, il mittente riceverebbe un ack per ogni utente
         connesso eligibile alla ricezione del messaggio */
      message_t ack;
      memset(&ack, 0, sizeof(message_t));
      ack.hdr.op = OP_OK;

      ret = send_handle_disconnect(pkt->fd, &ack, pkt->pl);
      HANDLE_FATAL(ret, "send_handle_disconnect");
    }
  }
}

void handle_post_txt(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == POSTTXT_OP);

  message_packet_t pkt;
  memset(&pkt, 0, sizeof(message_packet_t));
  pkt.pl = pl;
  pkt.message = *msg;
  pkt.fd = fd;

  int ret = chash_get(pl->registered_clients, msg->data.hdr.receiver, route_message_to_client, &pkt);
  HANDLE_FATAL(ret, "chash_get");
}

void handle_post_txt_all(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == POSTTXTALL_OP);

  message_packet_t pkt;
  memset(&pkt, 0, sizeof(message_packet_t));
  pkt.pl = pl;
  pkt.message = *msg;
  pkt.fd = fd;
  pkt.broadcast = 1;

  int ret = chash_get_all(pl->registered_clients, route_message_to_client, &pkt);
  HANDLE_FATAL(ret, "chash_get_all");

  message_t ack;
  memset(&ack, 0, sizeof(message_t));
  ack.hdr.op = OP_OK;

  int res = send_handle_disconnect(fd, &ack, pl);
  HANDLE_FATAL(res, "send_handle_disconnect");
}

void handle_post_file(long fd, message_t *msg, payload_t *pl) {}

void handle_get_file(long fd, message_t *msg, payload_t *pl) {}

static void handle_get_prev_msgs_cb(const char *key, void *value, void *ud) {
  assert(ud != NULL);
  struct callback_data *data = (struct callback_data*)ud;

  if(value == NULL) {
    /* È stata richiesta la cronologia di un nickname non registrato */
    INCREASE_ERRORS(data->pl);
    
    send_error_message(data->fd, OP_NICK_UNKNOWN, data->pl, key, "Nickname non esistente");
  } else {
    client_descriptor_t *cd = (client_descriptor_t *)value;

    void **elems;
    int numMsgs = ccircbuf_get_elems(cd->message_buffer, &elems);
    HANDLE_FATAL(numMsgs, "ccircbuf_get_elems");
    
    for(int i = 0; i < numMsgs; i++) {
      message_t *elem = (message_t*)elems[i];

      /* TODO */

      free(elem);
    }

    free(elems);
  }
}

void handle_get_prev_msgs(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == GETPREVMSGS_OP);

  struct callback_data data;
  data.fd = fd;
  data.pl = pl;

  int ret = chash_get(pl->registered_clients, msg->hdr.sender, handle_get_prev_msgs_cb, &data);
  HANDLE_FATAL(ret, "chash_get");
}

void handle_usr_list(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == USRLIST_OP);

  send_user_list(fd, pl);
}

void handle_unregister(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == UNREGISTER_OP);

  client_descriptor_t *deletedUser;

  HANDLE_FATAL(chash_set(pl->registered_clients, msg->data.hdr.receiver, NULL, (void*)(&deletedUser)), "chash_set");
  if(deletedUser == NULL) {
    /* Tentativo di deregistrazione di un nickname non registrato */
    INCREASE_ERRORS(pl);
    
    send_error_message(fd, OP_NICK_UNKNOWN, pl, msg->hdr.sender, "Nickname non esistente");
  } else {
    free_client_descriptor(deletedUser);
    message_t ack;
    memset(&ack, 0, sizeof(message_t));
    strncpy(ack.data.hdr.receiver, msg->hdr.sender, MAX_NAME_LENGTH);
    ack.hdr.op = OP_OK;

    int res = send_handle_disconnect(fd, &ack, pl);
    HANDLE_FATAL(res, "send_handle_disconnect");
  }
}

void handle_disconnect(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == DISCONNECT_OP);

  disconnect_client(fd, pl);
}

void handle_create_group(long fd, message_t *msg, payload_t *pl) {}

void handle_add_group(long fd, message_t *msg, payload_t *pl) {}

void handle_del_group(long fd, message_t *msg, payload_t *pl) {}

/* Inizializza la lookup-table dei gestori di richieste */
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