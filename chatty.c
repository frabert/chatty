/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */
/**
 * @file chatty.c
 * @brief File principale del server chatterbox
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "stats.h"
#include "cfgparse.h"
#include "errman.h"
#include "cqueue.h"
#include "chash.h"
#include "connections.h"

#define MAX_PATH_LEN 128
#define SHOULD_EXIT (signalStatus == SIGINT)


/* struttura che memorizza le statistiche del server, struct statistics 
 * e' definita in stats.h.
 *
 */
struct statistics chattyStats = { 0,0,0,0,0,0,0 };

volatile sig_atomic_t signalStatus = 0;

/**
 * \struct server_cfg
 * \brief Dati letti dai file di configurazione
 */
struct server_cfg {
    char socketPath[MAX_PATH_LEN];
    int maxConnections;
    int threadsInPool;
    int maxMsgSize;
    int maxFileSize;
    int maxHistMsgs;
    char dirName[MAX_PATH_LEN];
    char statFileName[MAX_PATH_LEN];
};

/**
 * \struct client_descriptor
 * \brief Rappresenta un utente registrato
 */
typedef struct client_descriptor {
    cqueue_t *pending_messages; //< Coda dei messaggi da inviare al client
    int fd; //< Descrittore del socket al client
} client_descriptor_t;

/**
 * \struct worker_thread_payload
 * \brief Dati da passare ai thread come contesto di lavoro
 */
typedef struct worker_thread_payload {
    cqueue_t *pending_messages; //< Coda dei messaggi da smistare
    chash_t *registered_clients; //< Tabella degli utenti registrati
} payload_t;

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

static void usage(const char *progname) {
    fprintf(stderr, "Il server va lanciato con il seguente comando:\n");
    fprintf(stderr, "  %s -f conffile\n", progname);
}

/// Legge interamente un file di configurazione e ne effettua il parsing
static int read_cfg_file(const char *file, struct server_cfg *cfg) {
    FILE *f = fopen(file, "r");
    fseek(f, 0, SEEK_END);
    // Vado fino in fondo al file per sapere quanto è lungo
    long fsize = ftell(f);
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
 * \fn setup_signal_handlers
 * \brief Imposta gli handler per i segnali da gestire
 */
static void setup_signal_handlers() {
    struct sigaction s;
    memset(&s, 0, sizeof(s));
    s.sa_handler = signal_handler;
    HANDLE_FATAL(sigaction(SIGINT, &s, NULL), "sigaction");
    s.sa_handler = SIG_IGN;
    HANDLE_FATAL(sigaction(SIGPIPE, &s, NULL), "sigaction");
}

void *worker_thread(void *data) {
    payload_t *pl = (payload_t*)data;
    while(!SHOULD_EXIT) {
        message_t msg;
        while(readMsg(pl->fd, &msg) > 0) {

        }
    }
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

    /* Elimina il file socket se fosse già presente */
    unlink(cfg.socketPath);

    cqueue_t *clients_queue = cqueue_init();

    cqueue_deinit(clients_queue, free);
    return 0;
}
