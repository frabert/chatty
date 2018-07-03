/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */
/**
 * \brief File principale del server chatterbox
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#include "stats.h"
#include "cfgparse.h"
#include "errman.h"
#include "cqueue.h"
#include "chash.h"
#include "ccircbuf.h"
#include "connections.h"

#include "chatty_handlers.h"

/// Consente di sapere se è stato ricevuto un segnale di terminazione
#define SHOULD_EXIT ((signalStatus == SIGINT) || (signalStatus == SIGQUIT) || (signalStatus == SIGTERM))

/// Controlla la validità di un valore di configurazione
#define CHECK_VAL(x) if(!(x)) { \
                       errno = EINVAL; \
                       return 0; \
                     }

/// Contiene l'ultimo segnale ricevuto
volatile sig_atomic_t signalStatus = 0;

void free_client_descriptor(void *ptr) {
  if(ptr == NULL) return;

  client_descriptor_t *cd = (client_descriptor_t*)ptr;
  void** messages;
  int numMsg = ccircbuf_get_elems(cd->message_buffer, &messages);
  HANDLE_FATAL(numMsg, "ccircbuf_get_elems");

  for(int i = 0; i < numMsg; i++) {
    message_t *msg = messages[i];
    free(msg->data.buf);
    free(messages[i]);
  }
  free(messages);
  HANDLE_FATAL(ccircbuf_deinit(cd->message_buffer), "ccircbuf_deinit");

  free(cd);
}

void free_group(void *ptr) {
  if(ptr == NULL) return;

  cstrlist_t *list = (cstrlist_t*)ptr;
  int ret = cstrlist_deinit(list);
  HANDLE_FATAL(ret, "cstrlist_deinit");
}

/**
 * \brief Callback che legge le impostazioni
 * 
 * \param name Nome del parametro
 * \param value Valore del parametro
 * \param ud Puntatore alla struttura che contiene le impostazioni
 * \return int 0 in caso di successo, -1 se l'impostazione non è valida
 */
static int load_cfg(const char *name, const char *value, void *ud) {
  struct server_cfg *cfg = (struct server_cfg *)ud;
  int valueLen = strlen(value);

  /* Il valore non può essere memorizzato nella struttura */
  if(valueLen > MAX_PATH_LEN) {
    errno = ENAMETOOLONG;
    return -1;
  }

  if(strcmp(name, "UnixPath") == 0) {
    /* Il valore va copiato perchè name e value vengono deallocati
       subito dopo la terminazione della funzione */
    memcpy(cfg->socketPath, value, sizeof(char) * valueLen + 1);
  } else if(strcmp(name, "MaxConnections") == 0) {
    cfg->maxConnections = atoi(value);
    CHECK_VAL(cfg->maxConnections);
  } else if(strcmp(name, "ThreadsInPool") == 0) {
    cfg->threadsInPool = atoi(value);
    CHECK_VAL(cfg->threadsInPool);
  } else if(strcmp(name, "MaxMsgSize") == 0) {
    cfg->maxMsgSize = atoi(value);
    CHECK_VAL(cfg->maxMsgSize);
  } else if(strcmp(name, "MaxFileSize") == 0) {
    cfg->maxFileSize = atoi(value);
    CHECK_VAL(cfg->maxFileSize);
  } else if(strcmp(name, "MaxHistMsgs") == 0) {
    cfg->maxHistMsgs = atoi(value);
    CHECK_VAL(cfg->maxHistMsgs);
  } else if(strcmp(name, "DirName") == 0) {
    memcpy(cfg->dirName, value, sizeof(char) * valueLen + 1);
  } else if(strcmp(name, "StatFileName") == 0) {
    memcpy(cfg->statFileName, value, sizeof(char) * valueLen + 1);
  } else {
    /* Opzione non riconosciuta */
    errno = EINVAL;
    return 0;
  }

  return 1;
}

/**
 * \brief Mostra il messaggio di istruzioni d'uso
 * 
 * \param progname Il path dell'eseguibile
 */
static void usage(const char *progname) {
  fprintf(stderr, "Il server va lanciato con il seguente comando:\n");
  fprintf(stderr, "  %s -f conffile\n", progname);
}

/**
 * \brief Legge interamente un file di configurazione
 * 
 * \param file Il file da leggere
 * \param cfg Struttura dove immagazzinare le impostazioni lette
 * \return int >= 0 se l'operazione ha avuto successo, -1 altrimenti.
 */
static int read_cfg_file(const char *file, struct server_cfg *cfg) {
  FILE *f = fopen(file, "r");
  fseek(f, 0, SEEK_END);
  // Vado fino in fondo al file per sapere quanto è lungo
  long fsize = ftell(f);
  // Torno all'inizio per leggerlo interamente
  fseek(f, 0, SEEK_SET);

  char *string = calloc(fsize + 1, sizeof(char));
  if(string == NULL) {
    return -1;
  }
  fread(string, fsize, 1, f);
  fclose(f);

  string[fsize] = 0;

  int res = cfg_parse(string, load_cfg, cfg);
  free(string);

  return res;
}

/**
 * \brief Gestore dei segnali minimale
 * 
 * Lascia tutto il lavoro di gestione al resto del programma
 * 
 * \param signal Il segnale da gestire
 */
static void signal_handler(int signal) {
  signalStatus = signal;
}

/**
 * \brief Imposta gli handler per i segnali da gestire
 */
static void setup_signal_handlers() {
  struct sigaction s;
  memset(&s, 0, sizeof(s));
  s.sa_handler = signal_handler;
  HANDLE_FATAL(sigaction(SIGINT, &s, NULL), "sigaction");
  HANDLE_FATAL(sigaction(SIGQUIT, &s, NULL), "sigaction");
  HANDLE_FATAL(sigaction(SIGTERM, &s, NULL), "sigaction");
  HANDLE_FATAL(sigaction(SIGUSR1, &s, NULL), "sigaction");

  s.sa_handler = SIG_IGN; /* Ignora SIGPIPE */
  HANDLE_FATAL(sigaction(SIGPIPE, &s, NULL), "sigaction");
}

/**
 * \brief Funzione eseguita dai thread nel pool
 * 
 * \param data Puntatore alla struttura che fornisce il contesto su cui lavorare
 * \return void* Sempre NULL
 */
void *worker_thread(void *data) {
  payload_t *pl = (payload_t*)data;
  while(!SHOULD_EXIT) {
    long *elem;
    HANDLE_FATAL(cqueue_pop(pl->ready_sockets, (void*)&elem), "cqueue_pop");

    long fd = *elem;
    if(fd == -1) {
      /*
       * Segnale speciale di uscita, viene reimmesso in coda
       * prima di uscire in modo da renderlo disponibile al
       * prossimo thread in attesa
       */
      cqueue_push(pl->ready_sockets, elem);
      return NULL;
    }
    free(elem);

    message_t msg;
    memset(&msg, 0, sizeof(message_t));

    int res = readMsg(fd, &msg);
    if(HAS_DISCONNECTED(res)) {
      LOG_ERR("%s", strerror(errno));
      disconnect_client(fd, pl, NULL);
      continue;
    } else HANDLE_FATAL(res, "readMsg");

    if(res == 0) {
      /* Il client si è disconnesso */
      disconnect_client(fd, pl, NULL);
    } else {
      if(msg.hdr.op >= OP_CLIENT_END) {
        LOG_WARN("Ricevuto messaggio non valido dal client %ld", fd);
        MUTEX_GUARD(pl->connected_clients_mtx, {
          send_error_message(fd, OP_FAIL, pl, NULL, "Messaggio non valido", NULL);
        });
      } else {
        if(msg.hdr.sender[0] != '\0') {
          /* Esegue il gestore di richieste in base all'operazione */
          int is_connected = 1;
          chatty_handlers[msg.hdr.op](fd, &msg, pl, &is_connected);
          if(is_connected)
            FD_SET(fd, &(pl->set));
        } else {
          /* Messaggio spurio, ignorato */
          LOG_INFO("Messaggio spurio da %ld ignorato", fd);
        }
      }
    }
    free(msg.data.buf);
  }

  return NULL;
}

/** Funzione d'entrata */
int main(int argc, char *argv[]) {
  if(argc != 3 || strcmp(argv[1], "-f") != 0) {
    usage(argv[0]);
    return -1;
  }

  struct server_cfg cfg;
  memset(&cfg, 0, sizeof(cfg));

  printf("Caricamento file di configurazione... ");

  int res = read_cfg_file(argv[2], &cfg);

  if(res <= 0) {
    perror("read_cfg_file");
    fprintf(stderr, "Errore durante la lettura di %s\n",
      argv[2]);
    return -1;
  }

  printf("fatto.\nInizializzazione del server... ");

  setup_signal_handlers();

  /* Conterrà i dati condivisi dai vari thread */
  payload_t payload;
  memset(&payload, 0, sizeof(payload_t));
  payload.registered_clients = chash_init();
  HANDLE_NULL(payload.registered_clients, "chash_init");

  payload.groups = chash_init();
  HANDLE_NULL(payload.groups, "chash_init");

  payload.ready_sockets = cqueue_init();
  HANDLE_NULL(payload.ready_sockets, "cqueue_init");

  HANDLE_FATAL(pthread_mutex_init(&(payload.connected_clients_mtx), NULL), "pthread_mutex_init");
  HANDLE_FATAL(pthread_mutex_init(&(payload.stats_mtx), NULL), "pthread_mutex_init");

  payload.connected_clients = calloc(cfg.maxConnections, sizeof(connected_client_t));
  HANDLE_NULL(payload.connected_clients, "calloc");

  payload.cfg = &cfg;

  for(int i = 0; i < cfg.maxConnections; i++) {
    payload.connected_clients[i].fd = -1;
  }

  pthread_t *threadPool = calloc(cfg.threadsInPool, sizeof(pthread_t));
  HANDLE_NULL(threadPool, "calloc");

  int fd_sk;
  struct sockaddr_un sa;
  fd_set readSet;

  /* Creazione del socket */
  strncpy(sa.sun_path, cfg.socketPath, MAX_PATH_LEN);
  sa.sun_family = AF_UNIX;
  fd_sk = socket(AF_UNIX, SOCK_STREAM, 0);
  HANDLE_FATAL(fd_sk, "socket");
  res = bind(fd_sk, (struct sockaddr *)&sa, sizeof(sa));
  HANDLE_FATAL(res, "bind");
  res = listen(fd_sk, SOMAXCONN);
  HANDLE_FATAL(res, "listen");
  
  FD_ZERO(&(payload.set));
  FD_SET(fd_sk, &(payload.set));

  for(int i = 0; i < cfg.threadsInPool; i++) {
    pthread_create(threadPool + i, NULL, worker_thread, &payload);
  }

  printf(" pronto. Server in ascolto.\n");

  while(!SHOULD_EXIT) {
    if(signalStatus == SIGUSR1) {
      /* È stato ricevuto il segnale di scrittura delle statistiche */
      FILE *statFile = fopen(cfg.statFileName, "a");
      HANDLE_NULL(statFile, "fopen");

      MUTEX_GUARD(payload.stats_mtx, {
        HANDLE_FATAL(printStats(statFile, &(payload.chatty_stats)), "printStats");
      });

      fclose(statFile);

      signalStatus = 0;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;

    readSet = payload.set;

    res = select(FD_SETSIZE, &readSet, NULL, NULL, &timeout);
    if(res < 0) {
      /*
       * La select ha fallito per qualche motivo, forse
       * perchè è stata interrotta: se così fosse SHOULD_EXIT adesso
       * è true e il programma deve terminare, altrimenti ritenta la select
       */
      continue;
    }

    for (long i = 0; i < FD_SETSIZE; ++i) {
      if (FD_ISSET(i, &readSet)) {
        if (i == fd_sk) {
          /* Connessione da un nuovo client */
          int newClient = accept(fd_sk, NULL, 0);
          HANDLE_FATAL(newClient, "accept");

          MUTEX_GUARD(payload.stats_mtx, {
            if(payload.chatty_stats.nonline >= cfg.maxConnections) {
              /* Numero massimo di connessioni raggiunto, rifiuta la connessione */
              LOG_ERR("Connessione di %d rifiutata", newClient);
  
              message_t errMsg;
              make_error_message(&errMsg, OP_FAIL, NULL, "Server occupato");
              sendRequest(newClient, &errMsg);
              
              free(errMsg.data.buf);
  
              payload.chatty_stats.nerrors++;
            } else {
              FD_SET(newClient, &(payload.set));
            }
          });
        } else {
          /* Un client già connesso è pronto */
          long *elem = calloc(1, sizeof(long));
          HANDLE_NULL(elem, "calloc");

          *elem = i;

          FD_CLR(i, &payload.set);
          cqueue_push(payload.ready_sockets, elem);
        }
      }
    }
  }

  /* Mette in coda un messaggio speciale per segnalare ai thread
     di terminare */
  cqueue_clear(payload.ready_sockets, free);
  long *elem = calloc(1, sizeof(long));
  HANDLE_NULL(elem, "calloc");
  *elem = -1;
  cqueue_push(payload.ready_sockets, elem);

  printf("\nChiusura in corso... ");

  for(int i = 0; i < cfg.threadsInPool; i++) {
    pthread_join(threadPool[i], NULL);
  }
  free(threadPool);
  
  printf("fatto.\nPulizia in corso... ");

  /* Elimina il file socket */
  unlink(cfg.socketPath);

  /* Pulizia finale delle risorse allocate */
  HANDLE_FATAL(chash_deinit(payload.registered_clients, free_client_descriptor), "chash_deinit");
  HANDLE_FATAL(chash_deinit(payload.groups, free_group), "chash_deinit");
  HANDLE_FATAL(cqueue_deinit(payload.ready_sockets, free), "cqueue_deinit");
  HANDLE_FATAL(pthread_mutex_destroy(&(payload.stats_mtx)), "pthread_mutex_destroy");

  free(payload.connected_clients);
  printf("fatto. Bye!\n");
  return 0;
}
