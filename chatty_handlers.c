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

/// Aumenta in maniera atomica il numero di errori inviati
#define INCREASE_ERRORS(pl) HANDLE_FATAL(pthread_mutex_lock(&((pl)->stats_mtx)), "pthread_mutex_lock"); \
                            (pl)->chatty_stats.nerrors++; \
                            HANDLE_FATAL(pthread_mutex_unlock(&((pl)->stats_mtx)), "pthread_mutex_unlock");

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
  if(ret == 0 || (ret < 0 && errno == EBADF)) {
    disconnect_client(fd, pl);
    ret = 0;
  }

  return ret;
}

/**
 * \brief Invia un header ad un client e gestisce automaticamente l'evenutale disconnessione
 * 
 * \param fd Descrittore a cui inviare l'header
 * \param msg L'header da inviare
 * \param pl Dati di contesto
 * \return int 0 se il client si è disconnesso, -1 in caso di errore (ed errno impostato), altro altrimenti
 */
static int send_header_handle_disconnect(long fd, message_hdr_t *msg, payload_t *pl) {
  int ret = sendHeader(fd, msg);
  if(ret == 0 || (ret < 0 && errno == EBADF)) {
    disconnect_client(fd, pl);
    ret = 0;
  }

  return ret;
}

void send_error_message(long fd, op_t error, payload_t *pl, const char *receiver, const char *text) {
  message_t errMsg;
  make_error_message(&errMsg, error, receiver, text);

  int ret = send_handle_disconnect(fd, &errMsg, pl);
  HANDLE_FATAL(ret, "Inviando un errore");
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
static int find_connected_client(long fd, payload_t *pl) {
  for(int i = 0; i < pl->cfg->maxConnections; i++) {
    connected_client_t *client = &(pl->connected_clients[i]);
    if(client->fd == fd) {
      return i;
    }
  }
  return -1;
}

void disconnect_client(long fd, payload_t *pl) {
  HANDLE_FATAL(pthread_mutex_lock(&(pl->stats_mtx)), "pthread_mutex_lock");
  pl->chatty_stats.nonline--;
  
  HANDLE_FATAL(pthread_mutex_lock(&(pl->connected_clients_mtx)), "pthread_mutex_lock");

  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx != -1) {
    LOG_INFO("Disconnessione di '%s'", pl->connected_clients[clientIdx].nick);
    pl->connected_clients[clientIdx].fd = -1;
    chash_get(pl->registered_clients, pl->connected_clients[clientIdx].nick, disconnect_client_cb, pl);
    pl->connected_clients[clientIdx].nick[0] = '\0';
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
    if(pl->connected_clients[i].fd > 0 && pl->connected_clients[i].nick[0] != '\0') {
      assert(c < num);
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
 * \brief Dati passati dalle funzioni di gestione alle callback
 */
struct callback_data {
  int fd; ///< Descrittore del client che ha effettutato la richiesta
  payload_t *pl; ///< Dati di contesto
  void *data; ///< Dati supplementari opzionali
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
    LOG_WARN("Tentativo di connessione di '%s' fallito", key);

    INCREASE_ERRORS(data->pl);

    send_error_message(fd, OP_NICK_UNKNOWN, data->pl, NULL, "Nickname non esistente");
    return;
  } else {
    client_descriptor_t *cd = (client_descriptor_t *)value;

    cd->fd = fd;
    for(int i = 0; i < data->pl->cfg->maxConnections; i++) {
      connected_client_t *client = &(data->pl->connected_clients[i]);
      if(client->fd < 0) {
        LOG_INFO("Utente '%s' connesso", key);
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
    LOG_WARN("Tentativo di registrazione di '%s' fallito", msg->hdr.sender);

    INCREASE_ERRORS(pl);

    send_error_message(fd, OP_NICK_ALREADY, pl, NULL, "Nickname già registrato");

    free_client_descriptor(cd);
  } else {
    LOG_INFO("Utente '%s' registrato", msg->hdr.sender);

    HANDLE_FATAL(pthread_mutex_lock(&(pl->stats_mtx)), "pthread_mutex_lock");
    pl->chatty_stats.nusers++;
    HANDLE_FATAL(pthread_mutex_unlock(&(pl->stats_mtx)), "pthread_mutex_unlock");

    /* Oltre a registrare il nick, connette il client */
    struct callback_data data;
    data.pl = pl;
    data.fd = fd;

    int ret = chash_get(pl->registered_clients, msg->hdr.sender, handle_connect_cb, &data);
    HANDLE_FATAL(ret, "chash_get");
  }
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

  if(pkt->sent) {
    /* Il messaggio è già stato inoltrato agli utenti di un gruppo, non va inviato di nuovo */
    return;
  }

  if(client == NULL) {
    /* Il messaggio è stato instradato verso un utente non esistente */
    INCREASE_ERRORS(pkt->pl);

    LOG_WARN("'%s' ha tentato di inviare un messaggio ad un utente inesistente ('%s')",
      pkt->message.hdr.sender, key);
    
    send_error_message(pkt->fd, OP_NICK_UNKNOWN, pkt->pl, key, "Nickname non esistente");
  } else {
    LOG_INFO("%s (%ld) -> %s (%ld): %s",
      pkt->message.hdr.sender,
      pkt->fd,
      key,
      client->fd,
      pkt->message.data.buf);

    message_t *msg = calloc(1, sizeof(message_t));
    memcpy(msg, &(pkt->message), sizeof(message_t));
    msg->data.buf = calloc(msg->data.hdr.len, sizeof(char));
    HANDLE_NULL(msg->data.buf, "calloc");

    memcpy(msg->data.buf, pkt->message.data.buf, msg->data.hdr.len);

    message_t *oldMsg = NULL;

    int ret = ccircbuf_insert(client->message_buffer, msg, (void*)&oldMsg);
    HANDLE_FATAL(ret, "ccircbuf_insert");

    if(oldMsg != NULL) {
      free(oldMsg->data.buf);
      free(oldMsg);
    }

    if(client->fd > 0) {
      /* Il client è connesso, gli invio il messaggio */
      message_t newMsg;
      memcpy(&newMsg, &(pkt->message), sizeof(message_t));

      if(newMsg.hdr.op == POSTTXT_OP || newMsg.hdr.op == POSTTXTALL_OP) {
        newMsg.hdr.op = TXT_MESSAGE;
      } else {
        newMsg.hdr.op = FILE_MESSAGE;
      }

      ret = send_handle_disconnect(client->fd, &newMsg, pkt->pl);
      HANDLE_FATAL(ret, "send_handle_disconnect");
    }

    if(!(pkt->broadcast)) {
      /* Se inviassi un ack qua, il mittente riceverebbe un ack per ogni utente
         connesso eligibile alla ricezione del messaggio */
      message_hdr_t ack;
      memset(&ack, 0, sizeof(message_hdr_t));
      ack.op = OP_OK;

      ret = send_header_handle_disconnect(pkt->fd, &ack, pkt->pl);
      HANDLE_FATAL(ret, "send_header_handle_disconnect");
    }
  }
}

/**
 * \brief Instrada un messaggio verso un gruppo
 * 
 * \param key Il nome del gruppo verso cui instradare il messaggio
 * \param value Puntatore alla lista degli utenti verso cui instradare
 * \param ud Puntatore al pacchetto da instradare
 */
static void route_message_to_group(const char *key, void *value, void *ud) {
  cstrlist_t *list = (cstrlist_t*)value;
  message_packet_t *pkt = (message_packet_t*)ud;

  if(list == NULL) {
    /* Il gruppo non esiste, ignoriamo */
    return;
  }

  pkt->broadcast = 1;
  char **users;

  int ret, num = cstrlist_get_values(list, &users);
  HANDLE_FATAL(num, "cstrlist_get_values");

  for(int i = 0; i < num; i++) {
    ret = chash_get(pkt->pl->registered_clients, users[i], route_message_to_client, pkt);
    HANDLE_FATAL(ret, "chash_get");

    free(users[i]);
  }
  free(users);
  pkt->sent = 1;

  message_hdr_t ack;
  memset(&ack, 0, sizeof(message_hdr_t));
  ack.op = OP_OK;

  ret = send_header_handle_disconnect(pkt->fd, &ack, pkt->pl);
  HANDLE_FATAL(ret, "send_header_handle_disconnect");
}

void handle_post_txt(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == POSTTXT_OP);

  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di inviare un messaggio */
    LOG_WARN("Il client %ld non connesso ha tentato di inviare un messaggio", fd);
    send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso");
    return;
  }

  if(msg->data.hdr.len > pl->cfg->maxMsgSize) {
    /* Il messaggio è troppo lungo */
    LOG_WARN("'%s' ha tentato di inviare un messaggio troppo lungo", pl->connected_clients[clientIdx].nick);
    send_error_message(fd, OP_MSG_TOOLONG, pl, NULL, "Messaggio testuale troppo lungo");
    return;
  }

  message_packet_t pkt;
  memset(&pkt, 0, sizeof(message_packet_t));
  pkt.pl = pl;
  pkt.message = *msg;
  pkt.fd = fd;

  /* Prima tentiamo di inviare il messaggio ad un gruppo */
  int ret = chash_get(pl->groups, msg->data.hdr.receiver, route_message_to_group, &pkt);
  HANDLE_FATAL(ret, "chash_get");

  /* Se il gruppo non esiste, tentiamo di inviare il messaggio ad un utente registrato */
  ret = chash_get(pl->registered_clients, msg->data.hdr.receiver, route_message_to_client, &pkt);
  HANDLE_FATAL(ret, "chash_get");
}

void handle_post_txt_all(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == POSTTXTALL_OP);

  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di inviare un messaggio */
    LOG_WARN("Il client %ld non connesso ha tentato di inviare un messaggio broadcast", fd);
    send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso");
    return;
  }

  if(msg->data.hdr.len > pl->cfg->maxMsgSize) {
    /* Il messaggio è troppo lungo */
    LOG_WARN("'%s' ha tentato di inviare un messaggio broadcast troppo lungo",
      pl->connected_clients[clientIdx].nick);
    send_error_message(fd, OP_MSG_TOOLONG, pl, NULL, "Messaggio testuale troppo lungo");
    return;
  }

  message_packet_t pkt;
  memset(&pkt, 0, sizeof(message_packet_t));
  pkt.pl = pl;
  pkt.message = *msg;
  pkt.fd = fd;
  pkt.broadcast = 1;

  int ret = chash_get_all(pl->registered_clients, route_message_to_client, &pkt);
  HANDLE_FATAL(ret, "chash_get_all");

  message_hdr_t ack;
  memset(&ack, 0, sizeof(message_hdr_t));
  ack.op = OP_OK;

  int res = send_header_handle_disconnect(fd, &ack, pl);
  HANDLE_FATAL(res, "send_header_handle_disconnect");
}

/**
 * \brief Restituisce il nome di un file senza il percorso
 * 
 * Percorre all'indietro il percorso del file e si ferma al primo '/' trovato
 * 
 * \param file_path Il percorso da cui estrarre il nome
 * \param len Restituisce la lunghezza del nome
 * \return const char* L'indirizzo al primo carattere del nome del file.
 */
static const char *strip_file_name(const char * file_path, size_t *len) {
  size_t path_len = strlen(file_path);
  *len = 0;
  const char *ptr = file_path + path_len;
  while(ptr > file_path) {
    ptr--;
    *len = *len + 1;
    if(*ptr == '/') {
      ptr++;
    *len = *len - 1;
      break;
    }
  }
  return ptr;
}

void handle_post_file(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == POSTFILE_OP);

  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di inviare un file */
    LOG_WARN("Il client %ld non connesso ha tentato di inviare un file", fd);
    send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso");
    return;
  }

  message_data_t file_data;
  memset(&file_data, 0, sizeof(message_data_t));
  int ret = readData(fd, &file_data);
  HANDLE_FATAL(ret, "readData");
  if(ret == 0) {
    disconnect_client(fd, pl);
    return;
  }

  if(file_data.hdr.len > pl->cfg->maxFileSize * 1000) {
    /* Il file è troppo lungo */
    LOG_WARN("'%s' ha tentato di inviare un file troppo lungo",
      pl->connected_clients[clientIdx].nick);
    send_error_message(fd, OP_MSG_TOOLONG, pl, NULL, "File troppo lungo");
    free(file_data.buf);
    return;
  }

  size_t file_name_len = 0, dir_path_len = strnlen(pl->cfg->dirName, MAX_PATH_LEN);
  const char *file_name = strip_file_name(msg->data.buf, &file_name_len);

  /* Crea il path completo del file nella directory specificata nelle impostazioni */
  char *file_path = calloc(file_name_len + dir_path_len + 2, sizeof(char));
  strncpy(file_path, pl->cfg->dirName, dir_path_len);
  strncat(file_path, "/", 1);
  strncat(file_path, file_name, file_name_len);

  LOG_INFO("Tentativo di scrittura del file %s", file_path);
  /* Scrive il file nella directory */
  FILE *file = fopen(file_path, "wb");
  HANDLE_NULL(file, "fopen");
  fwrite(file_data.buf, sizeof(char), file_data.hdr.len, file);
  fclose(file);
  file = NULL;

  free(file_path);
  free(file_data.buf);

  message_packet_t pkt;
  memset(&pkt, 0, sizeof(message_packet_t));
  pkt.pl = pl;
  pkt.message = *msg;
  pkt.fd = fd;

  pkt.message.data.buf = (char*)file_name;
  pkt.message.data.hdr.len = file_name_len + 1;

  /* Prima tentiamo di inviare il messaggio ad un gruppo */
  ret = chash_get(pl->groups, msg->data.hdr.receiver, route_message_to_group, &pkt);
  HANDLE_FATAL(ret, "chash_get");

  /* Se non esiste il gruppo, tentiamo l'invio ad un utente */
  ret = chash_get(pl->registered_clients, msg->data.hdr.receiver, route_message_to_client, &pkt);
  HANDLE_FATAL(ret, "chash_get");
}

void handle_get_file(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == GETFILE_OP);

  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di inviare un file */
    LOG_WARN("Il client %ld non connesso ha tentato di ricevere un file", fd);
    send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso");
    return;
  }

  size_t file_name_len = msg->data.hdr.len;
  size_t dir_path_len = strnlen(pl->cfg->dirName, MAX_PATH_LEN);

  /* Crea il path completo del file nella directory specificata nelle impostazioni */
  char *file_path = calloc(file_name_len + dir_path_len + 2, sizeof(char));
  strncpy(file_path, pl->cfg->dirName, dir_path_len);
  strncat(file_path, "/", 1);
  strncat(file_path, msg->data.buf, file_name_len);

  /* Scrive il file nella directory */
  FILE *file = fopen(file_path, "wb");
  free(file_path);
  if(file == NULL) {
    LOG_WARN("'%s' ha richiesto un file non disponibile", pl->connected_clients[clientIdx].nick);
    send_error_message(fd, OP_FAIL, pl, NULL, "Impossibile accedere al file richiesto");
    return;
  }

  fseek(file, 0, SEEK_END);
  /* Vado fino in fondo al file per sapere quanto è lungo */
  long fsize = ftell(file);
  /* Torno all'inizio per leggerlo interamente */
  fseek(file, 0, SEEK_SET);

  /* Alloco il buffer di risposta */
  char *data = calloc(fsize, sizeof(char));
  HANDLE_NULL(data, "calloc");
  
  fread(data, fsize, 1, file);
  fclose(file);
  file = NULL;

  LOG_INFO("'%s' ha richiesto un file", pl->connected_clients[clientIdx].nick);

  message_t answer;
  memset(&answer, 0, sizeof(message_t));
  answer.data.buf = data;
  answer.data.hdr.len = fsize;
  answer.hdr.op = OP_OK;

  send_handle_disconnect(fd, &answer, pl);
  free(data);
}

static void handle_get_prev_msgs_cb(const char *key, void *value, void *ud) {
  assert(ud != NULL);
  struct callback_data *data = (struct callback_data*)ud;

  if(value == NULL) {
    /* È stata richiesta la cronologia di un nickname non registrato */
    INCREASE_ERRORS(data->pl);
    LOG_WARN("E' stata richiesta la cronologia dell'utente '%s' non esistente", key);

    send_error_message(data->fd, OP_NICK_UNKNOWN, data->pl, key, "Nickname non esistente");
  } else {
    LOG_INFO("'%s' ha richiesto la cronologia", key);

    client_descriptor_t *cd = (client_descriptor_t *)value;

    void **elems;
    int numMsgs = ccircbuf_get_elems(cd->message_buffer, &elems);
    HANDLE_FATAL(numMsgs, "ccircbuf_get_elems");

    size_t *buf = calloc(1, sizeof(size_t));
    *buf = numMsgs;

    message_t ack;
    memset(&ack, 0, sizeof(message_t));
    ack.hdr.op = OP_OK;
    ack.data.buf = (char*)buf;
    ack.data.hdr.len = sizeof(size_t);

    int ret = send_handle_disconnect(data->fd, &ack, data->pl);
    free(buf);
    if(ret == 0) {
      return;
    }

    int disconnected = 0;
    
    for(int i = 0; i < numMsgs; i++) {
      message_t *elem = (message_t*)elems[i];

      if(!disconnected) {
        disconnected = send_handle_disconnect(data->fd, elem, data->pl) == 0;
      }
    }

    free(elems);
  }
}

void handle_get_prev_msgs(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == GETPREVMSGS_OP);

  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di richiedere la cronologia */
    LOG_WARN("Il client %ld non connesso ha richiesto la cronologia di un utente", fd);
    send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso");
    return;
  }

  struct callback_data data;
  data.fd = fd;
  data.pl = pl;

  int ret = chash_get(pl->registered_clients, msg->hdr.sender, handle_get_prev_msgs_cb, &data);
  HANDLE_FATAL(ret, "chash_get");
}

void handle_usr_list(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == USRLIST_OP);

  LOG_INFO("Il client '%ld' ha richiesto la lista degli utenti connessi", fd);
  send_user_list(fd, pl);
}

/**
 * \brief Viene chiamata da \ref handle_unregister nel momento in cui un utente
 *        si deregistra per eliminarlo da tutti i gruppi
 * 
 * \param key Il nome del gruppo
 * \param value Il gruppo contenente una lista di utenti
 * \param ub Il nickname dell'utente deregistrato
 */
static void handle_unregister_cb(const char *key, void *value, void *ub) {
  cstrlist_t *group = (cstrlist_t*)value;
  char *name = (char*)ub;

  int ret = cstrlist_remove(group, name);
  if(ret != 0) {
    if(errno == ENOENT) {
      /* L'utente che si è deregistrato non era in questo gruppo, ignoriamo */
    } else {
      HANDLE_FATAL(ret, "cstrlist_remove");
    }
  }
}

void handle_unregister(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == UNREGISTER_OP);

  client_descriptor_t *deletedUser;

  HANDLE_FATAL(chash_set(pl->registered_clients, msg->data.hdr.receiver, NULL, (void*)(&deletedUser)), "chash_set");
  if(deletedUser == NULL) {
    /* Tentativo di deregistrazione di un nickname non registrato */
    INCREASE_ERRORS(pl);
    LOG_WARN("Tentativo di deregistrazione di '%s' non esistente", msg->data.hdr.receiver);
    send_error_message(fd, OP_NICK_UNKNOWN, pl, msg->hdr.sender, "Nickname non esistente");
  } else {
    LOG_INFO("Deregistrazione di '%s'", msg->data.hdr.receiver);
    free_client_descriptor(deletedUser);

    /* Elimina l'utente deregistrato da tutti i gruppi */
    int res = chash_get_all(pl->groups, handle_unregister_cb, msg->data.hdr.receiver);
    HANDLE_FATAL(res, "chash_get_all");

    message_t ack;
    memset(&ack, 0, sizeof(message_t));
    strncpy(ack.data.hdr.receiver, msg->hdr.sender, MAX_NAME_LENGTH);
    ack.hdr.op = OP_OK;

    res = send_handle_disconnect(fd, &ack, pl);
    HANDLE_FATAL(res, "send_handle_disconnect");
  }
}

void handle_disconnect(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == DISCONNECT_OP);

  disconnect_client(fd, pl);
}

void handle_create_group(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == CREATEGROUP_OP);

  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di creare un gruppo */
    LOG_WARN("Il client %ld non connesso ha tentato di creare un gruppo", fd);
    send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso");
    return;
  }

  cstrlist_t *list = cstrlist_init();
  int res = chash_set_if_empty(pl->groups, msg->data.hdr.receiver, list);
  if(res == 0) {
    LOG_INFO("Gruppo %s creato da %s (%ld)", msg->data.hdr.receiver, "", fd);

    message_hdr_t ack;
    memset(&ack, 0, sizeof(message_hdr_t));
    ack.op = OP_OK;

    res = send_header_handle_disconnect(fd, &ack, pl);
    HANDLE_FATAL(res, "send_header_handle_disconnect");
  } else if(res == 1) {
    LOG_WARN("Gruppo %s di %s (%ld) già esistente", msg->data.hdr.receiver, msg->hdr.sender, fd);
    cstrlist_deinit(list);

    send_error_message(fd, OP_FAIL, pl, NULL, "Gruppo già esistente");
  } else {
    HANDLE_FATAL(res, "chash_set_if_empty");
  }
}

/**
 * \brief Funzione chiamata da handle_add_group per eseguire l'aggiunta di un
 *        utente ad un gruppo
 * 
 * \param key Il nome del gruppo a cui è stata tentata l'aggiuna
 * \param value Il gruppo a cui aggiungere l'utente. NULL se il gruppo non esiste
 * \param ud Dati di contesto (tipo: \ref callback_data*)
 */
static void handle_add_group_cb(const char *key, void *value, void *ud) {
  struct callback_data *cbdata = (struct callback_data*)ud;
  cstrlist_t *list = (cstrlist_t*)value;

  if(list == NULL) {
    /* Tentativo di aggiungere un utente ad un gruppo non esistente */
    LOG_WARN("Il client %d ha tentato di aggiungersi ad un gruppo non esistente", cbdata->fd);

    send_error_message(cbdata->fd, OP_FAIL, cbdata->pl, NULL, "Gruppo inesistente");
  } else {
    int res = cstrlist_insert(list, cbdata->data);
    if(res != 0) {
      if(errno == EALREADY) {
        LOG_WARN("Il client %d ha tentato di aggiungersi nuovamente ad un gruppo", cbdata->fd);

        send_error_message(cbdata->fd, OP_FAIL, cbdata->pl, NULL, "Utente già presente nel gruppo");
      } else {
        HANDLE_FATAL(res, "cstrlist_insert");
      }
    } else {
      message_hdr_t ack;
      memset(&ack, 0, sizeof(message_hdr_t));
      ack.op = OP_OK;

      res = send_header_handle_disconnect(cbdata->fd, &ack, cbdata->pl);
      HANDLE_FATAL(res, "send_header_handle_disconnect");
    }
  }
}

void handle_add_group(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == ADDGROUP_OP);

  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di aggiungersi ad un gruppo */
    LOG_WARN("Il client %ld non connesso ha tentato di aggiungersi ad un gruppo", fd);
    send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso");
    return;
  }

  struct callback_data cbdata;
  memset(&cbdata, 0, sizeof(struct callback_data));
  cbdata.pl = pl;
  cbdata.fd = fd;
  cbdata.data = msg->hdr.sender;

  int res = chash_get(pl->groups, msg->data.hdr.receiver, handle_add_group_cb, &cbdata);
  HANDLE_FATAL(res, "chash_get");
}

/**
 * \brief Funzione chiamata da handle_del_group per eseguire la rimozione di un
 *        utente da un gruppo
 * 
 * \param key Il nome del gruppo da cui è stata tentata la rimozione
 * \param value Il gruppo da cui rimuovere l'utente. NULL se il gruppo non esiste
 * \param ud Dati di contesto (tipo: \ref callback_data*)
 */
static void handle_del_group_cb(const char *key, void *value, void *ud) {
  struct callback_data *cbdata = (struct callback_data*)ud;
  cstrlist_t *list = (cstrlist_t*)value;

  if(list == NULL) {
    /* Tentativo di rimozione di un utente da un gruppo non esistente */
    LOG_WARN("Il client %d ha tentato di rimuoversi da un gruppo non esistente", cbdata->fd);

    send_error_message(cbdata->fd, OP_FAIL, cbdata->pl, NULL, "Gruppo inesistente");
  } else {
    int res = cstrlist_remove(list, cbdata->data);
    if(res != 0) {
      if(errno == ENOENT) {
        LOG_WARN("Il client %d ha tentato di rimuoversi da un gruppo a cui non era iscritto", cbdata->fd);

        send_error_message(cbdata->fd, OP_FAIL, cbdata->pl, NULL, "Utente non presente nel gruppo");
      } else {
        HANDLE_FATAL(res, "cstrlist_remove");
      }
    } else {
      message_hdr_t ack;
      memset(&ack, 0, sizeof(message_hdr_t));
      ack.op = OP_OK;

      res = send_header_handle_disconnect(cbdata->fd, &ack, cbdata->pl);
      HANDLE_FATAL(res, "send_header_handle_disconnect");
    }
  }
}

void handle_del_group(long fd, message_t *msg, payload_t *pl) {
  assert(msg->hdr.op == DELGROUP_OP);

  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di rimuoversi da un gruppo */
    LOG_WARN("Il client %ld non connesso ha tentato di rimuoversi da un gruppo", fd);
    send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso");
    return;
  }

  struct callback_data cbdata;
  memset(&cbdata, 0, sizeof(struct callback_data));
  cbdata.pl = pl;
  cbdata.fd = fd;
  cbdata.data = msg->hdr.sender;

  int res = chash_get(pl->groups, msg->data.hdr.receiver, handle_del_group_cb, &cbdata);
  HANDLE_FATAL(res, "chash_get");
}

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