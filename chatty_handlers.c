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
#define INCREASE_ERRORS(pl) MUTEX_GUARD((pl)->stats_mtx, { (pl)->chatty_stats.nerrors++; });

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
}

/**
 * \brief Invia un messaggio ad un client e gestisce automaticamente l'evenutale disconnessione
 * 
 * \param fd Descrittore a cui inviare il messaggio
 * \param msg Il messaggio da inviare
 * \param pl Dati di contesto
 * \return int 0 se il client si è disconnesso, -1 in caso di errore (ed errno impostato), altro altrimenti
 */
static int send_handle_disconnect(long fd, message_t *msg, payload_t *pl, client_descriptor_t *client) {
  int ret = sendRequest(fd, msg);
  if(ret == 0 || HAS_DISCONNECTED(ret)) {
    LOG_ERR("%s", strerror(errno));
    disconnect_client(fd, pl, client);
    ret = 0;
  }

  return ret;
}

/**
 * \brief Invia un header ad un client e gestisce automaticamente l'evenutale disconnessione
 * 
 * \warning Questa procedura presuppone che il chiamante abbia bloccato \ref connected_clients_mtx
 * 
 * \param fd Descrittore a cui inviare l'header
 * \param msg L'header da inviare
 * \param pl Dati di contesto
 * \return int 0 se il client si è disconnesso, -1 in caso di errore (ed errno impostato), altro altrimenti
 */
static int send_header_handle_disconnect(long fd, message_hdr_t *msg, payload_t *pl, client_descriptor_t *client) {
  int ret = sendHeader(fd, msg);
  if(ret == 0 || HAS_DISCONNECTED(ret)) {
    LOG_ERR("%s", strerror(errno));
    disconnect_client(fd, pl, client);
    ret = 0;
  }

  return ret;
}

int send_error_message(long fd, op_t error, payload_t *pl, const char *receiver, const char *text, client_descriptor_t *client) {
  message_t errMsg;
  make_error_message(&errMsg, error, receiver, text);

  int ret = send_handle_disconnect(fd, &errMsg, pl, client);
  HANDLE_FATAL(ret, "Inviando un errore");

  INCREASE_ERRORS(pl);

  if(errMsg.data.buf) free(errMsg.data.buf);

  return ret;
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

  if(value == NULL) {
    return;
  }

  client_descriptor_t *cd = (client_descriptor_t*)value;
  cd->fd = -1;
}

/**
 * \brief Restituisce il client associato al descrittore \ref fd
 * 
 * \warning Questa procedura presuppone che il chiamante abbia bloccato \ref connected_clients_mtx
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

void disconnect_client(long fd, payload_t *pl, client_descriptor_t *client) {
  int clientIdx = find_connected_client(fd, pl);
  if(clientIdx != -1) {
    LOG_INFO("Disconnessione di '%s'", pl->connected_clients[clientIdx].nick);

    if(client == NULL) {
      int ret = chash_get(pl->registered_clients,
                          pl->connected_clients[clientIdx].nick,
                          disconnect_client_cb, pl);
      HANDLE_FATAL(ret, "chash_get");
    } else {
      client->fd = -1;
    }

    pl->connected_clients[clientIdx].nick[0] = '\0';
    pl->connected_clients[clientIdx].fd = -1;

    MUTEX_GUARD(pl->stats_mtx, {
      if(pl->chatty_stats.nonline > 0)
        pl->chatty_stats.nonline--;
    });
  }
}

/**
 * \brief Invia la lista degli utenti registrati
 * 
 * \warning Questa procedura presuppone che il chiamante abbia bloccato \ref connected_clients_mtx
 * 
 * \param fd Il client a cui inviare la lista
 * \param pl Dati di contesto
 */
static int send_user_list(int fd, payload_t *pl, client_descriptor_t *client) {
  char *buf = NULL;
  int c = 0;

  buf = calloc(sizeof(char) * (MAX_NAME_LENGTH + 1), pl->cfg->maxConnections);
  for(int i = 0; i < pl->cfg->maxConnections; i++) {
    if(pl->connected_clients[i].fd > 0 && pl->connected_clients[i].nick[0] != '\0') {
      strncpy(buf + (MAX_NAME_LENGTH + 1) * c, pl->connected_clients[i].nick, MAX_NAME_LENGTH);
      c++;
    }
  }

  message_t msg;
  memset(&msg, 0, sizeof(message_t));
  msg.hdr.op = OP_OK;
  msg.data.hdr.len = sizeof(char) * (MAX_NAME_LENGTH + 1) * c;
  msg.data.buf = buf;

  int ret;

  ret = send_handle_disconnect(fd, &msg, pl, client);
  HANDLE_FATAL(ret, "send_handle_disconnect");
  
  free(buf);
  
  return ret;
}

/**
 * \brief Dati passati dalle funzioni di gestione alle callback
 */
struct callback_data {
  int fd; ///< Descrittore del client che ha effettutato la richiesta
  payload_t *pl; ///< Dati di contesto
  void *data; ///< Dati supplementari opzionali
  int *is_connected; 
};

/**
 * \brief Viene utilizzata da \ref handle_connect per associare un socket a un nome utente
 * 
 * \warning Questa procedura presuppone che il chiamante abbia bloccato \ref connected_clients_mtx
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

    *(data->is_connected) |= send_error_message(fd, OP_NICK_UNKNOWN, data->pl, NULL, "Nickname non esistente", NULL);
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

        *(data->is_connected) |= send_user_list(fd, data->pl, cd);

        MUTEX_GUARD(data->pl->stats_mtx, {
          data->pl->chatty_stats.nonline++;
        });

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
void handle_connect(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == CONNECT_OP);

  struct callback_data data;
  data.pl = pl;
  data.fd = fd;
  data.is_connected = is_connected;
  
  MUTEX_GUARD(pl->connected_clients_mtx, {
    int ret = chash_get(pl->registered_clients, msg->hdr.sender, handle_connect_cb, &data);
    HANDLE_FATAL(ret, "chash_get");
  });
}

/**
 * \brief Registra il nickname \ref nick
 * 
 * \param msg Il messaggio ricevuto
 * \param fd Il descrittore a cui inviare il messaggio d'errore
 *           nel caso in cui il nickname fosse già registrato
 * \param pl Informazioni di contesto
 */
void handle_register(long fd, message_t *msg, payload_t *pl, int *is_connected) {
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
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_NICK_ALREADY, pl, NULL, "Nickname già registrato", NULL);
    });

    free_client_descriptor(cd);
  } else {
    LOG_INFO("Utente '%s' registrato", msg->hdr.sender);

    MUTEX_GUARD(pl->stats_mtx, {
      pl->chatty_stats.nusers++;
    });

    /* Oltre a registrare il nick, connette il client */
    struct callback_data data;
    data.pl = pl;
    data.fd = fd;
    data.is_connected = is_connected;
    
    MUTEX_GUARD(pl->connected_clients_mtx, {
      int ret = chash_get(pl->registered_clients, msg->hdr.sender, handle_connect_cb, &data);
      HANDLE_FATAL(ret, "chash_get");
    });
  }
}

/**
 * \brief Instrada un messaggio verso un client
 * 
 * \warning Questa procedura presuppone che il chiamante abbia bloccato \ref connected_clients_mtx
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
    
    *(pkt->is_connected) |= send_error_message(pkt->fd, OP_NICK_UNKNOWN, pkt->pl, key, "Nickname non esistente", client);
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

      ret = send_handle_disconnect(client->fd, &newMsg, pkt->pl, client);
      HANDLE_FATAL(ret, "send_handle_disconnect");

      MUTEX_GUARD(pkt->pl->stats_mtx, {
        if(msg->hdr.op == POSTTXT_OP) {
          if(ret == 0) {
            pkt->pl->chatty_stats.nnotdelivered++;
          } else {
            pkt->pl->chatty_stats.ndelivered++;
          }
        } else {
          if(ret == 0) {
            pkt->pl->chatty_stats.nfilenotdelivered++;
          } else {
            pkt->pl->chatty_stats.nfiledelivered++;
          }
        }
      });

    } else {
      MUTEX_GUARD(pkt->pl->stats_mtx, {
        if(msg->hdr.op == POSTTXT_OP)
          pkt->pl->chatty_stats.nnotdelivered++;
        else
          pkt->pl->chatty_stats.nfilenotdelivered++;
      });
    }

    if(!(pkt->broadcast)) {
      /* Se inviassi un ack qua, il mittente riceverebbe un ack per ogni utente
         connesso eligibile alla ricezione del messaggio */
      message_hdr_t ack;
      memset(&ack, 0, sizeof(message_hdr_t));
      ack.op = OP_OK;

      ret = send_header_handle_disconnect(pkt->fd, &ack, pkt->pl, client);
      HANDLE_FATAL(ret, "send_header_handle_disconnect");
      *(pkt->is_connected) |= ret;
    }
  }
}

/**
 * \brief Instrada un messaggio verso un gruppo
 * 
 * \warning Questa procedura presuppone che il chiamante abbia bloccato \ref connected_clients_mtx
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

  int is_in_group = 0;
  for(int i = 0; i < num; i++) {
    if(strncmp(pkt->message.hdr.sender, users[i], MAX_NAME_LENGTH) == 0) {
      is_in_group = 1;
      break;
    }
  }

  if(is_in_group) {
    for(int i = 0; i < num; i++) {
      ret = chash_get(pkt->pl->registered_clients, users[i], route_message_to_client, pkt);
      HANDLE_FATAL(ret, "chash_get");
      free(users[i]);
    }

    message_hdr_t ack;
    memset(&ack, 0, sizeof(message_hdr_t));
    ack.op = OP_OK;

    ret = send_header_handle_disconnect(pkt->fd, &ack, pkt->pl, NULL);
    HANDLE_FATAL(ret, "send_header_handle_disconnect");
    *(pkt->is_connected) |= ret;
  } else {
    for(int i = 0; i < num; i++) {
      free(users[i]);
    }

    ret = send_error_message(pkt->fd, OP_NICK_UNKNOWN, pkt->pl,
                             pkt->message.hdr.sender, "Client non registrato al gruppo", NULL);
    HANDLE_FATAL(ret, "send_error_message");
    *(pkt->is_connected) |= ret;
  }
  free(users);
  pkt->sent = 1;
}

/**
 * \brief Gestisce l'invio di un messaggio testuale
 * 
 * \param fd Il descrittore del client che ha richiesto l'invio
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_post_txt(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == POSTTXT_OP);

  int clientIdx = -1;
  MUTEX_GUARD(pl->connected_clients_mtx, {
    clientIdx = find_connected_client(fd, pl);
  });

  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di inviare un messaggio */
    LOG_WARN("Il client %ld non connesso ha tentato di inviare un messaggio", fd);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso", NULL);
    });
    return;
  }

  if(msg->data.hdr.len > pl->cfg->maxMsgSize) {
    /* Il messaggio è troppo lungo */
    LOG_WARN("'%s' ha tentato di inviare un messaggio troppo lungo", pl->connected_clients[clientIdx].nick);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_MSG_TOOLONG, pl, NULL, "Messaggio testuale troppo lungo", NULL);
    });
    return;
  }

  message_packet_t pkt;
  memset(&pkt, 0, sizeof(message_packet_t));
  pkt.pl = pl;
  pkt.message = *msg;
  pkt.fd = fd;
  pkt.is_connected = is_connected;

  MUTEX_GUARD(pl->connected_clients_mtx, {
    /* Prima tentiamo di inviare il messaggio ad un gruppo */
    int ret = chash_get(pl->groups, msg->data.hdr.receiver, route_message_to_group, &pkt);
    HANDLE_FATAL(ret, "chash_get");

    /* Se il gruppo non esiste, tentiamo di inviare il messaggio ad un utente registrato */
    ret = chash_get(pl->registered_clients, msg->data.hdr.receiver, route_message_to_client, &pkt);
    HANDLE_FATAL(ret, "chash_get");
  });
  
}

/**
 * \brief Gestisce una richiesta di invio messaggio broadcast
 * 
 * \param fd Il descrittore del client che ha richiesto l'operazione
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_post_txt_all(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == POSTTXTALL_OP);

  int clientIdx = -1;
  MUTEX_GUARD(pl->connected_clients_mtx, {
    clientIdx = find_connected_client(fd, pl);
  });

  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di inviare un messaggio */
    LOG_WARN("Il client %ld non connesso ha tentato di inviare un messaggio broadcast", fd);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso", NULL);
    });
    return;
  }

  if(msg->data.hdr.len > pl->cfg->maxMsgSize) {
    /* Il messaggio è troppo lungo */
    LOG_WARN("'%s' ha tentato di inviare un messaggio broadcast troppo lungo",
      pl->connected_clients[clientIdx].nick);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_MSG_TOOLONG, pl, NULL, "Messaggio testuale troppo lungo", NULL);
    });
    return;
  }

  message_packet_t pkt;
  memset(&pkt, 0, sizeof(message_packet_t));
  pkt.pl = pl;
  pkt.message = *msg;
  pkt.fd = fd;
  pkt.broadcast = 1;
  pkt.is_connected = is_connected;

  MUTEX_GUARD(pl->connected_clients_mtx, {
    int ret = chash_get_all(pl->registered_clients, route_message_to_client, &pkt);
    HANDLE_FATAL(ret, "chash_get_all");
  });

  message_hdr_t ack;
  memset(&ack, 0, sizeof(message_hdr_t));
  ack.op = OP_OK;

  int res;
  MUTEX_GUARD(pl->connected_clients_mtx, {
    res = send_header_handle_disconnect(fd, &ack, pl, 0);
  });
  HANDLE_FATAL(res, "send_header_handle_disconnect");
  *is_connected |= res;
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

/**
 * \brief Gestisce una richiesta di invio file
 * 
 * \param fd Il descrittore del client che ha richiesto l'operazione
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_post_file(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == POSTFILE_OP);

  int clientIdx = -1;
  MUTEX_GUARD(pl->connected_clients_mtx, {
    clientIdx = find_connected_client(fd, pl);
  });

  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di inviare un file */
    LOG_WARN("Il client %ld non connesso ha tentato di inviare un file", fd);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso", NULL);
    });
    return;
  }

  message_data_t file_data;
  memset(&file_data, 0, sizeof(message_data_t));
  int ret = readData(fd, &file_data);
  if(ret == 0 || HAS_DISCONNECTED(ret)) {
    disconnect_client(fd, pl, NULL);
    *is_connected = 0; 
    return;
  }
  HANDLE_FATAL(ret, "readData");

  if(file_data.hdr.len > pl->cfg->maxFileSize * 1000) {
    /* Il file è troppo lungo */
    LOG_WARN("'%s' ha tentato di inviare un file troppo lungo",
      pl->connected_clients[clientIdx].nick);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_MSG_TOOLONG, pl, NULL, "File troppo lungo", NULL);
    });
    free(file_data.buf);
    return;
  }

  size_t file_name_len = 0, dir_path_len = strlen(pl->cfg->dirName);
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
  pkt.is_connected = is_connected;

  pkt.message.data.buf = (char*)file_name;
  pkt.message.data.hdr.len = file_name_len + 1;

  MUTEX_GUARD(pl->connected_clients_mtx, {
    /* Prima tentiamo di inviare il messaggio ad un gruppo */
    ret = chash_get(pl->groups, msg->data.hdr.receiver, route_message_to_group, &pkt);
    HANDLE_FATAL(ret, "chash_get");

    /* Se non esiste il gruppo, tentiamo l'invio ad un utente */
    ret = chash_get(pl->registered_clients, msg->data.hdr.receiver, route_message_to_client, &pkt);
    HANDLE_FATAL(ret, "chash_get");
  });
}

/**
 * \brief Gestisce una richiesta di ricezione file
 * 
 * \param fd Il descrittore del client che ha richiesto l'operazione
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_get_file(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == GETFILE_OP);

  int clientIdx = -1;
  MUTEX_GUARD(pl->connected_clients_mtx, {
    clientIdx = find_connected_client(fd, pl);
  });

  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di inviare un file */
    LOG_WARN("Il client %ld non connesso ha tentato di ricevere un file", fd);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso", NULL);
    });
    return;
  }

  size_t file_name_len = msg->data.hdr.len;
  size_t dir_path_len = strlen(pl->cfg->dirName);

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
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Impossibile accedere al file richiesto", NULL);
    });
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

  MUTEX_GUARD(pl->connected_clients_mtx, {
    *is_connected |= send_handle_disconnect(fd, &answer, pl, NULL);
  });
  free(data);
}

/**
 * \brief Viene chiamata da \ref handle_get_prev_msgs
 * 
 * \warning Questa procedura presuppone che il chiamante abbia bloccato \ref connected_clients_mtx
 * 
 * \param key Nickname dell'utente di cui ottenere la cronologia
 * \param value Utente di cui ottenere la cronologia, NULL se non esistente
 * \param ud Dati di contesto
 */
static void handle_get_prev_msgs_cb(const char *key, void *value, void *ud) {
  assert(ud != NULL);
  struct callback_data *data = (struct callback_data*)ud;

  if(value == NULL) {
    /* È stata richiesta la cronologia di un nickname non registrato */
    INCREASE_ERRORS(data->pl);
    LOG_WARN("E' stata richiesta la cronologia dell'utente '%s' non esistente", key);

    *(data->is_connected) |= send_error_message(data->fd, OP_NICK_UNKNOWN, data->pl, key, "Nickname non esistente", NULL);
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

    int ret = send_handle_disconnect(data->fd, &ack, data->pl, cd);
    free(buf);
    if(ret == 0) {
      *(data->is_connected) = 0;
      return;
    }

    int connected = 1;
    
    for(int i = 0; i < numMsgs; i++) {
      message_t *elem = (message_t*)elems[i];

      if(connected) {
        connected = send_handle_disconnect(data->fd, elem, data->pl, cd);
      }
    }
    *(data->is_connected) |= connected;

    free(elems);
  }
}

/**
 * \brief Gestisce una richiesta di ricezione della cronologia
 * 
 * \param fd Descrittore del client che ha richiesto l'operazione
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_get_prev_msgs(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == GETPREVMSGS_OP);

  int clientIdx = -1;
  MUTEX_GUARD(pl->connected_clients_mtx, {
    clientIdx = find_connected_client(fd, pl);
  });

  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di richiedere la cronologia */
    LOG_WARN("Il client %ld non connesso ha richiesto la cronologia di un utente", fd);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso", NULL);
    });
    return;
  }

  struct callback_data data;
  data.fd = fd;
  data.pl = pl;
  data.is_connected = is_connected;

  MUTEX_GUARD(pl->connected_clients_mtx, {
    int ret = chash_get(pl->registered_clients, msg->hdr.sender, handle_get_prev_msgs_cb, &data);
    HANDLE_FATAL(ret, "chash_get");
  });
}

void handle_usr_list(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == USRLIST_OP);

  LOG_INFO("Il client '%ld' ha richiesto la lista degli utenti connessi", fd);
  MUTEX_GUARD(pl->connected_clients_mtx, {
    *is_connected = send_user_list(fd, pl, NULL);
  });
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

/**
 * \brief Gestisce una richiesta di deregistrazione
 * 
 * \param fd Il descrittore del client che ha richiesto l'operazione
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_unregister(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == UNREGISTER_OP);

  client_descriptor_t *deletedUser;

  HANDLE_FATAL(chash_set(pl->registered_clients, msg->data.hdr.receiver, NULL, (void*)(&deletedUser)), "chash_set");
  if(deletedUser == NULL) {
    /* Tentativo di deregistrazione di un nickname non registrato */
    INCREASE_ERRORS(pl);
    LOG_WARN("Tentativo di deregistrazione di '%s' non esistente", msg->data.hdr.receiver);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_NICK_UNKNOWN, pl, msg->hdr.sender, "Nickname non esistente", NULL);
    });
  } else {
    LOG_INFO("Deregistrazione di '%s'", msg->data.hdr.receiver);

    disconnect_client(fd, pl, deletedUser);
    free_client_descriptor(deletedUser);

    /* Elimina l'utente deregistrato da tutti i gruppi */
    int res = chash_get_all(pl->groups, handle_unregister_cb, msg->data.hdr.receiver);
    HANDLE_FATAL(res, "chash_get_all");

    message_t ack;
    memset(&ack, 0, sizeof(message_t));
    strncpy(ack.data.hdr.receiver, msg->hdr.sender, MAX_NAME_LENGTH);
    ack.hdr.op = OP_OK;

    MUTEX_GUARD(pl->connected_clients_mtx, {
      res = send_handle_disconnect(fd, &ack, pl, 0);
      HANDLE_FATAL(res, "send_handle_disconnect");
    });
    close(fd);

    *is_connected = 0;

    MUTEX_GUARD(pl->stats_mtx, {
      pl->chatty_stats.nusers--;
    });
  }
}

/**
 * \brief Gestisce una richiesta di disconnessione
 * 
 * \param fd Il descrittore del client che ha richiesto l'operazione
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_disconnect(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == DISCONNECT_OP);

  disconnect_client(fd, pl, NULL);
  close(fd);
  *is_connected = 0;
}

/**
 * \brief Gestisce una richiesta di creazione di un gruppo
 * 
 * \param fd Il descrittore del client che ha richiesto l'operazione
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_create_group(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == CREATEGROUP_OP);

  int clientIdx = -1;
  MUTEX_GUARD(pl->connected_clients_mtx, {
    clientIdx = find_connected_client(fd, pl);
  });

  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di creare un gruppo */
    LOG_WARN("Il client %ld non connesso ha tentato di creare un gruppo", fd);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso", NULL);
    });
    return;
  }

  cstrlist_t *list = cstrlist_init();
  int res = chash_set_if_empty(pl->groups, msg->data.hdr.receiver, list);
  if(res == 0) {
    LOG_INFO("Gruppo %s creato da %s (%ld)", msg->data.hdr.receiver, "", fd);

    message_hdr_t ack;
    memset(&ack, 0, sizeof(message_hdr_t));
    ack.op = OP_OK;

    MUTEX_GUARD(pl->connected_clients_mtx, {
      /* Aggiungo il creatore al gruppo */
      cstrlist_insert(list, pl->connected_clients[clientIdx].nick);

      res = send_header_handle_disconnect(fd, &ack, pl, 0);
    });
    HANDLE_FATAL(res, "send_header_handle_disconnect");
    *is_connected |= res;
  } else if(res == 1) {
    LOG_WARN("Gruppo %s di %s (%ld) già esistente", msg->data.hdr.receiver, msg->hdr.sender, fd);
    cstrlist_deinit(list);

    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Gruppo già esistente", NULL);
    });
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

    *(cbdata->is_connected) |= send_error_message(cbdata->fd, OP_FAIL, cbdata->pl, NULL, "Gruppo inesistente", NULL);
  } else {
    int res = cstrlist_insert(list, cbdata->data);
    if(res != 0) {
      if(errno == EALREADY) {
        LOG_WARN("Il client %d ha tentato di aggiungersi nuovamente ad un gruppo", cbdata->fd);

        *(cbdata->is_connected) |= send_error_message(cbdata->fd, OP_FAIL, cbdata->pl, NULL, "Utente già presente nel gruppo", NULL);
      } else {
        HANDLE_FATAL(res, "cstrlist_insert");
      }
    } else {
      message_hdr_t ack;
      memset(&ack, 0, sizeof(message_hdr_t));
      ack.op = OP_OK;

      res = send_header_handle_disconnect(cbdata->fd, &ack, cbdata->pl, 0);
      HANDLE_FATAL(res, "send_header_handle_disconnect");
      *(cbdata->is_connected) |= res;
    }
  }
}

/**
 * \brief Gestisce una richiesta di aggiunta ad un gruppo
 * 
 * \param fd Il descrittore del client che ha richiesto l'operazione
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_add_group(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == ADDGROUP_OP);

  int clientIdx = -1;
  MUTEX_GUARD(pl->connected_clients_mtx, {
    clientIdx = find_connected_client(fd, pl);
  });

  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di aggiungersi ad un gruppo */
    LOG_WARN("Il client %ld non connesso ha tentato di aggiungersi ad un gruppo", fd);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso", NULL);
    });
    return;
  }

  struct callback_data cbdata;
  memset(&cbdata, 0, sizeof(struct callback_data));
  cbdata.pl = pl;
  cbdata.fd = fd;
  cbdata.data = msg->hdr.sender;
  cbdata.is_connected = is_connected;

  MUTEX_GUARD(pl->connected_clients_mtx, {
    int res = chash_get(pl->groups, msg->data.hdr.receiver, handle_add_group_cb, &cbdata);
    HANDLE_FATAL(res, "chash_get");
  });
}

/**
 * \brief Funzione chiamata da handle_del_group per eseguire la rimozione di un
 *        utente da un gruppo
 * 
 * \warning Questa procedura presuppone che il chiamante abbia ottenuto
 *          il blocco su \ref connected_clients_mtx
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

    *(cbdata->is_connected) |= send_error_message(cbdata->fd, OP_FAIL, cbdata->pl, NULL, "Gruppo inesistente", NULL);
  } else {
    int res = cstrlist_remove(list, cbdata->data);
    if(res != 0) {
      if(errno == ENOENT) {
        LOG_WARN("Il client %d ha tentato di rimuoversi da un gruppo a cui non era iscritto", cbdata->fd);

        *(cbdata->is_connected) |= send_error_message(cbdata->fd, OP_FAIL, cbdata->pl, NULL, "Utente non presente nel gruppo", NULL);
      } else {
        HANDLE_FATAL(res, "cstrlist_remove");
      }
    } else {
      message_hdr_t ack;
      memset(&ack, 0, sizeof(message_hdr_t));
      ack.op = OP_OK;

      res = send_header_handle_disconnect(cbdata->fd, &ack, cbdata->pl, NULL);
      HANDLE_FATAL(res, "send_header_handle_disconnect");
      *(cbdata->is_connected) |= res;
    }
  }
}

/**
 * \brief Gestisce una richiesta di eliminazione da un gruppo
 * 
 * \param fd Il descrittore del client che ha richiesto l'eliminazione da un gruppo
 * \param msg Il messaggio ricevuto
 * \param pl Dati di contesto
 */
void handle_del_group(long fd, message_t *msg, payload_t *pl, int *is_connected) {
  assert(msg->hdr.op == DELGROUP_OP);

  int clientIdx = -1;
  MUTEX_GUARD(pl->connected_clients_mtx, {
    clientIdx = find_connected_client(fd, pl);
  });

  if(clientIdx == -1) {
    /* Un client non connesso ha tentato di rimuoversi da un gruppo */
    LOG_WARN("Il client %ld non connesso ha tentato di rimuoversi da un gruppo", fd);
    MUTEX_GUARD(pl->connected_clients_mtx, {
      *is_connected |= send_error_message(fd, OP_FAIL, pl, NULL, "Client non connesso", NULL);
    });
    return;
  }

  struct callback_data cbdata;
  memset(&cbdata, 0, sizeof(struct callback_data));
  cbdata.pl = pl;
  cbdata.fd = fd;
  cbdata.data = msg->hdr.sender;
  cbdata.is_connected = is_connected;

  MUTEX_GUARD(pl->connected_clients_mtx, {
    int res = chash_get(pl->groups, msg->data.hdr.receiver, handle_del_group_cb, &cbdata);
    HANDLE_FATAL(res, "chash_get");
  });
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