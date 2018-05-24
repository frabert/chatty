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
#include <string.h>
#include <time.h>

#include "stats.h"
#include "cfgparse.h"
#include "errman.h"
#include "cqueue.h"
#include "chash.h"
#include "ccircbuf.h"
#include "connections.h"

/// Massima lunghezza di un path nei file di configurazione
#define MAX_PATH_LEN 128

/// Consente di sapere se è stato ricevuto un segnale di terminazione
#define SHOULD_EXIT ((signalStatus == SIGINT) || (signalStatus == SIGQUIT) || (signalStatus == SIGTERM))


/* struttura che memorizza le statistiche del server, struct statistics 
 * e' definita in stats.h.
 *
 */
pthread_mutex_t statsMtx;
struct statistics chattyStats = { 0,0,0,0,0,0,0 };

volatile sig_atomic_t signalStatus = 0;

/**
 * \struct server_cfg
 * \brief Dati letti dai file di configurazione
 */
struct server_cfg {
    char socketPath[MAX_PATH_LEN]; //< Path del socket su cui effettuare la connessione
    int maxConnections; //< Massimo numero di client connessi ammesso
    int threadsInPool; //< Numero di threads da spawnare per gestire le connessioni
    int maxMsgSize; //< Massima lunghezza di un messaggio testuale
    int maxFileSize; //< Massima lunghezza di un file inviato
    int maxHistMsgs; //< Lunghezza massima della cronologia dei messaggi
    char dirName[MAX_PATH_LEN]; //< Nome della directory in cui depositare i file scambiati
    char statFileName[MAX_PATH_LEN]; //< Nome del file su cui memorizzare le statistiche
};

/**
 * \struct client_descriptor
 * \brief Rappresenta un utente registrato
 */
typedef struct client_descriptor {
    ccircbuf_t *message_buffer; //< Mantiene la cronologia dei messaggi (tipo: message_t*)
    long fd; //< Descrittore del socket al client
} client_descriptor_t;

/**
 * \struct message_packet
 * \brief Rappresenta un pacchetto da inviare a un client
 * 
 */
typedef struct message_packet {
    chash_t *registered_clients; //< Tabella degli utenti registrati (tipo: client_descriptor_t*)
    message_t message; //< Il messaggio da inviare
} message_packet_t;

/**
 * \struct worker_thread_payload
 * \brief Dati da passare ai thread come contesto di lavoro
 */
typedef struct worker_thread_payload {
    cqueue_t *ready_sockets; //< Coda dei socket pronti (tipo: int)
    chash_t *registered_clients; //< Tabella degli utenti registrati (tipo: client_descriptor_t*)
} payload_t;

/**
 * \brief Crea un messaggio d'errore da inviare ad un client
 * 
 * \param msg Messaggio da riempire
 * \param error Tipo di errore
 * \param receiver Nome del destinatario
 */
static void makeErrorMessage(message_t *msg, op_t error, const char *receiver) {
    memset(msg, 0, sizeof(message_t));
    msg->hdr.op = error;
    memcpy(&(msg->data.hdr.receiver), receiver, strlen(receiver));
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

    /* Se il ricevente e il mittente del messaggio coincidessero, si creerebbe una deadlock */
    assert(pkt->message.hdr.sender == NULL || (strcmp(key, pkt->message.hdr.sender) != 0));

    if(client == NULL) {
        /* Il messaggio è stato instradato verso un utente non esistente */

        if(pkt->message.hdr.sender == NULL) {
            /* Il server ha cercato di inviare un messaggio ad un client non esistente, ignoriamo */
            return;
        }
        message_packet_t errorPacket;
        makeErrorMessage(&(errorPacket.message), OP_NICK_UNKNOWN, key);

        HANDLE_FATAL(chash_get(pkt->registered_clients, pkt->message.hdr.sender, route_message_to_client, &errorPacket), "chash_get");
    } else {
        
        //HANDLE_FATAL(sendRequest(client->fd, &(pkt->message)), "sendRequest");
    }
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
    if(valueLen > MAX_PATH_LEN - 1) {
        return -1;
    }

    if(strcmp(name, "UnixPath") == 0) {
        /* Il valore va copiato perchè name e value vengono deallocati
           subito dopo la terminazione della funzione */
        memcpy(cfg->socketPath, value, sizeof(char) * valueLen + 1);
    } else if(strcmp(name, "MaxConnections") == 0) {
        cfg->maxConnections = atoi(value);
        if(!cfg->maxConnections) return 0;
    } else if(strcmp(name, "ThreadsInPool") == 0) {
        cfg->threadsInPool = atoi(value);
        if(!cfg->threadsInPool) return 0;
    } else if(strcmp(name, "MaxMsgSize") == 0) {
        cfg->maxMsgSize = atoi(value);
        if(!cfg->maxMsgSize) return 0;
    } else if(strcmp(name, "MaxFileSize") == 0) {
        cfg->maxFileSize = atoi(value);
        if(!cfg->maxFileSize) return 0;
    } else if(strcmp(name, "MaxHistMsgs") == 0) {
        cfg->maxHistMsgs = atoi(value);
        if(!cfg->maxHistMsgs) return 0;
    } else if(strcmp(name, "DirName") == 0) {
        memcpy(cfg->dirName, value, sizeof(char) * valueLen + 1);
    } else if(strcmp(name, "StatFileName") == 0) {
        memcpy(cfg->statFileName, value, sizeof(char) * valueLen + 1);
    } else {
        /* Opzione non riconosciuta */
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

    char *string = (char *)malloc(fsize + 1);
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
        void *elem;
        int res = cqueue_pop(pl->ready_sockets, &elem);
        HANDLE_FATAL(res, "cqueue_pop");

        int fd = (int)elem;
        if(fd == -1) {
            /*
             * Segnale speciale di uscita, viene reimmesso in coda
             * prima di uscire in modo da renderlo disponibile al
             * prossimo thread in attesa
             */
            cqueue_push(pl->ready_sockets, (void*)-1);
            return NULL;
        }

        message_t msg;
        res = readMsg(fd, &msg);
        if(res == 0) {
            printf("Client disconnesso\n");
        } else {
            printf("Ricevuto messaggio da %s\n", msg.hdr.sender);
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    if(argc != 3 || strcmp(argv[1], "-f") != 0) {
        usage(argv[0]);
        return -1;
    }

    struct server_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));

    int res = read_cfg_file(argv[2], &cfg);

    if(res <= 0) {
        fprintf(stderr, "Errore durante la lettura del file di configurazione %s\n",
            argv[2]);
        return -1;
    }

    setup_signal_handlers();

    res = pthread_mutex_init(&statsMtx, NULL);
    HANDLE_FATAL(res, "pthread_mutex_init");

    payload_t payload;
    memset(&payload, 0, sizeof(payload_t));
    payload.registered_clients = chash_init();
    HANDLE_NULL(payload.registered_clients, "chash_init");

    payload.ready_sockets = cqueue_init();
    HANDLE_NULL(payload.ready_sockets, "cqueue_init");

    pthread_t *threadPool = malloc(sizeof(pthread_t) * cfg.threadsInPool);
    HANDLE_NULL(threadPool, "malloc");

    int fd_sk, fd_c;
    struct sockaddr_un sa;
    fd_set set, rdSet;

    /* Creazione del socket */
    strncpy(sa.sun_path, cfg.socketPath, MAX_PATH_LEN);
    sa.sun_family = AF_UNIX;
    fd_sk = socket(AF_UNIX, SOCK_STREAM, 0);
    HANDLE_FATAL(fd_sk, "socket");
    res = bind(fd_sk, (struct sockaddr *)&sa, sizeof(sa));
    HANDLE_FATAL(res, "bind");
    res = listen(fd_sk, SOMAXCONN);
    HANDLE_FATAL(res, "listen");
    
    FD_ZERO(&set);
    FD_SET(fd_sk, &set); 

    for(int i = 0; i < cfg.threadsInPool; i++) {
        pthread_create(threadPool + i, NULL, worker_thread, &payload);
    }

    while(!SHOULD_EXIT) {
        if(signalStatus == SIGUSR1) {
            /* È stato ricevuto il segnale di scrittura delle statistiche */
            FILE *statFile = fopen(cfg.statFileName, "a");
            HANDLE_NULL(statFile, "fopen");

            res = pthread_mutex_lock(&statsMtx);
            HANDLE_FATAL(res, "pthread_mutex_lock");

            res = printStats(statFile);
            HANDLE_FATAL(res, "printStats");

            res = pthread_mutex_unlock(&statsMtx);
            HANDLE_FATAL(res, "pthread_mutex_unlock");

            fclose(statFile);

            signalStatus = 0;
        }

        rdSet = set;
        res = select(FD_SETSIZE, &rdSet, NULL, NULL, NULL);
        if(res < 0) {
            /*
             * La select ha fallito per qualche motivo, forse
             * perchè è stata interrotta: se così fosse SHOULD_EXIT adesso
             * è true e il programma deve terminare, altrimenti ritenta la select
             */
            continue;
        }

        for (int i = 0; i < FD_SETSIZE; ++i) {
            if (FD_ISSET(i, &rdSet)) {
                if (i == fd_sk) {
                    /* Connessione da un nuovo client */
                    int newClient;
                    newClient = accept(fd_sk, NULL, 0);
                    HANDLE_FATAL(newClient, "accept");

                    res = pthread_mutex_lock(&statsMtx);
                    HANDLE_FATAL(res, "pthread_mutex_lock");
                    if(chattyStats.nonline >= cfg.maxConnections) {
                        /* Numero massimo di connessioni raggiunto, rifiuta la connessione */
                        message_t errMsg;
                        makeErrorMessage(&errMsg, OP_FAIL, "");
                        sendRequest(newClient, &errMsg);
                        chattyStats.nerrors++;
                    } else {
                        chattyStats.nonline++;
                        FD_SET(newClient, &rdSet);
                    }

                    res = pthread_mutex_unlock(&statsMtx);
                    HANDLE_FATAL(res, "pthread_mutex_unlock");
                } else {
                    /* Un client già connesso è pronto */
                    cqueue_push(payload.ready_sockets, (void*)i);
                    FD_CLR(i, &rdSet);
                }
            }
        }
    }

    /* Mette in coda un messaggio speciale per segnalare ai thread
       di terminare */
    cqueue_push(payload.ready_sockets, (void*)-1);

    for(int i = 0; i < cfg.threadsInPool; i++) {
        pthread_join(threadPool[i], NULL);
    }
    
    printf("\nClosing\n");

    /* Elimina il file socket */
    unlink(cfg.socketPath);

    res = chash_deinit(payload.registered_clients, free);
    HANDLE_FATAL(res, "chash_deinit");

    res = cqueue_deinit(payload.ready_sockets, NULL);
    HANDLE_FATAL(res, "cqueue_deinit");

    res = pthread_mutex_destroy(&statsMtx);
    HANDLE_FATAL(res, "pthread_mutex_destroy");
    return 0;
}
