/*
 * chatterbox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */
#ifndef MESSAGE_H_
#define MESSAGE_H_

#include <assert.h>
#include <string.h>
#include <config.h>
#include <ops.h>

/**
 *  \brief Header del messaggio
 */
typedef struct {
    op_t op; ///< Tipo di operazione richiesta al aserver
    char sender[MAX_NAME_LENGTH+1]; ///< Nickname del mittente
} message_hdr_t;

/**
 *  \brief Header della parte dati
 */
typedef struct {
    char receiver[MAX_NAME_LENGTH+1]; ///< Nickname del destinatario
    unsigned int len; ///< Lunghezza del buffer dati
} message_data_hdr_t;

/**
 *  \brief Body del messaggio 
 */
typedef struct {
    message_data_hdr_t  hdr; ///< HEader della parte dati
    char *buf; ///< Buffer dati
} message_data_t;

/**
 *  \brief Tipo del messaggio
 */
typedef struct {
    message_hdr_t hdr; ///< Header
    message_data_t data; ///< Dati
} message_t;

/* ------ funzioni di utilità ------- */

/**
 * \brief scrive l'header del messaggio
 *
 * \param hdr puntatore all'header
 * \param op tipo di operazione da eseguire
 * \param sender mittente del messaggio
 */
static inline void setHeader(message_hdr_t *hdr, op_t op, char *sender) {
#if defined(MAKE_VALGRIND_HAPPY)
    memset((char*)hdr, 0, sizeof(message_hdr_t));
#endif
    hdr->op  = op;
    strncpy(hdr->sender, sender, strlen(sender)+1);
}
/**
 * \brief scrive la parte dati del messaggio
 *
 * \param data puntatore al body del messaggio
 * \param rcv nickname o groupname del destinatario
 * \param buf puntatore al buffer 
 * \param len lunghezza del buffer
 */
static inline void setData(message_data_t *data, char *rcv, const char *buf, unsigned int len) {
#if defined(MAKE_VALGRIND_HAPPY)
    memset((char*)&(data->hdr), 0, sizeof(message_data_hdr_t));
#endif

    strncpy(data->hdr.receiver, rcv, strlen(rcv)+1);
    data->hdr.len  = len;
    data->buf      = (char *)buf;
}



#endif /* MESSAGE_H_ */
