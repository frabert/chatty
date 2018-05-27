/**
 *  \author Francesco Bertolaccini 543981
 * 
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 *  \brief Gestione delle richieste
 */

#ifndef CHATTY_HANDLERS_H_
#define CHATTY_HANDLERS_H_

#include <sys/select.h>

#include "chash.h"
#include "cqueue.h"
#include "ccircbuf.h"

#include "stats.h"
#include "message.h"

/**
 * \struct server_cfg
 * \brief Dati letti dai file di configurazione
 */
struct server_cfg {
  char socketPath[MAX_PATH_LEN + 1]; ///< Path del socket su cui effettuare la connessione
  int maxConnections; ///< Massimo numero di client connessi ammesso
  int threadsInPool; ///< Numero di threads da spawnare per gestire le connessioni
  int maxMsgSize; ///< Massima lunghezza di un messaggio testuale
  int maxFileSize; ///< Massima lunghezza di un file inviato
  int maxHistMsgs; ///< Lunghezza massima della cronologia dei messaggi
  char dirName[MAX_PATH_LEN + 1]; ///< Nome della directory in cui depositare i file scambiati
  char statFileName[MAX_PATH_LEN + 1]; ///< Nome del file su cui memorizzare le statistiche
};

/**
 * \brief Rappresenta un utente registrato
 */
typedef struct {
  ccircbuf_t *message_buffer; ///< Mantiene la cronologia dei messaggi (tipo: \ref message_t*)
  long fd; ///< Descrittore del socket al client
} client_descriptor_t;

/**
 * \brief Rappresenta un client che si è connesso
 */
typedef struct {
  char nick[MAX_NAME_LENGTH + 1]; ///< Il nickname associato al client
  long fd; ///< Il socket associato al client
} connected_client_t;

/**
 * \brief Dati da passare ai thread come contesto di lavoro
 */
typedef struct {
  fd_set set; ///< Bitset dei descrittori su cui ascoltare

  cqueue_t *ready_sockets; ///< Coda dei socket pronti (tipo: int)
  chash_t *registered_clients; ///< Tabella degli utenti registrati (tipo: \ref client_descriptor_t*)
  connected_client_t *connected_clients; ///< Vettore di client connessi
  pthread_mutex_t connected_clients_mtx; ///< Mutex per l'accesso a \ref connected_clients
  struct server_cfg *cfg; ///< Parametri di configurazione del server

  pthread_mutex_t stats_mtx; ///< Mutex per l'accesso alle statistiche
  struct statistics chatty_stats; ///< Statistiche del server
} payload_t;

/**
 * \brief Rappresenta un pacchetto da inviare a un client
 */
typedef struct {
  payload_t *pl; ///< Dati di contesto
  message_t message; ///< Il messaggio da inviare
  long fd; ///< Il descrittore del mittente
  int broadcast; ///< 1 se il messaggio è diretto a più utenti, 0 altrimenti
} message_packet_t;

/**
 * \brief Gestisce la disconnessione di un client
 * 
 * \param fd Il socket che si è disconnesso
 * \param pl Informazioni di contesto
 */
void disconnect_client(long fd, payload_t *pl);

/**
 * \brief Dealloca una struttura \ref client_descriptor_t
 * 
 * \param ptr Puntatore alla struttura da deallocare
 */
void free_client_descriptor(void *ptr);

/**
 * \brief Funzione di gestione delle richieste
 */
typedef void(chatty_request_handler)(long fd, message_t *msg, payload_t *pl);

/**
 * \brief Vettore delle funzioni di gestione delle richieste
 */
chatty_request_handler *chatty_handlers[OP_END];

/**
 * \brief Crea un messaggio d'errore da inviare ad un client
 * 
 * \param msg Messaggio da riempire
 * \param error Tipo di errore
 * \param receiver Nome del destinatario (opzionale)
 * \param text Testo d'errore (opzionale)
 */
void make_error_message(message_t *msg, op_t error, const char *receiver, const char *text);

/**
 * \brief Invia un messaggio d'errore ad un client e gestisce automaticamente l'eventuale disconnessione
 * 
 * \param fd Il descrittore a cui inviare l'errore
 * \param error Il codice d'errore da inviare
 * \param pl Dati di contesto
 * \param receiver Nome del destinatario (opzionale)
 * \param text Testo d'errore (opzionale)
 */
void send_error_message(long fd, op_t error, payload_t *pl, const char *receiver, const char *text);

#endif