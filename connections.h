/*
 * chatterbox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */
#ifndef CONNECTIONS_H_
#define CONNECTIONS_H_

#define MAX_RETRIES     10
#define MAX_SLEEPING     3
#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX  64
#endif

#include <message.h>

/**
 * \brief Contiene le funzioni che implementano il protocollo 
 *        tra i clients ed il server
 */

/**
 * \brief Apre una connessione AF_UNIX verso il server 
 *
 * \param path Path del socket AF_UNIX 
 * \param ntimes numero massimo di tentativi di retry
 * \param secs tempo di attesa tra due retry consecutive
 *
 * \return il descrittore associato alla connessione in caso di successo
 *         -1 in caso di errore
 */
int openConnection(char* path, unsigned int ntimes, unsigned int secs);

// -------- server side ----- 
/**
 * \brief Legge l'header del messaggio
 *
 * \param fd     descrittore della connessione
 * \param hdr    puntatore all'header del messaggio da ricevere
 *
 * \return <=0 se c'e' stato un errore 
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa) 
 */
int readHeader(long fd, message_hdr_t *hdr);

/**
 * \brief Legge il body del messaggio
 *
 * \param fd     descrittore della connessione
 * \param data   puntatore al body del messaggio
 *
 * \return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa) 
 */
int readData(long fd, message_data_t *data);

/**
 * \brief Legge l'header dei dati del messaggio
 *
 * \param fd        descrittore della connessione
 * \param datahdr   puntatore all'header dei dati
 *
 * \return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa) 
 */
int readDataHeader(long fd, message_data_hdr_t *datahdr);

/**
 * \brief Legge l'intero messaggio
 *
 * \param fd     descrittore della connessione
 * \param msg    puntatore al messaggio
 *
 * \return <=0 se c'e' stato un errore
 *         (se <0 errno deve essere settato, se == 0 connessione chiusa) 
 */
int readMsg(long fd, message_t *msg);

/* da completare da parte dello studente con altri metodi di interfaccia */


// ------- client side ------
/**
 * \brief Invia un messaggio di richiesta al server 
 *
 * \param fd     descrittore della connessione
 * \param msg    puntatore al messaggio da inviare
 *
 * \return <0 se si è verificato un errore,
 *         =0 se il socket si è chiuso,
 *         >0 se l'operazione ha avuto successo
 */
int sendRequest(long fd, message_t *msg);

/**
 * \brief Invia il body del messaggio al server
 *
 * \param fd     descrittore della connessione
 * \param msg    puntatore al messaggio da inviare
 *
 * \return <0 se si è verificato un errore,
 *         =0 se il socket si è chiuso,
 *         >0 se l'operazione ha avuto successo
 */
int sendData(long fd, message_data_t *msg);

/**
 * \brief Invia un header di messaggio ad un descrittore
 * 
 * \param fd Il descrittore a cui inviare l'header
 * \param hdr L'header da inviare
 * \return <0 se si è verificato un errore,
 *         =0 se il socket si è chiuso,
 *         >0 se l'operazione ha avuto successo
 */
int sendHeader(long fd, message_hdr_t *hdr);

#endif /* CONNECTIONS_H_ */
