/**
 *  \file ccircbuf.h
 *  \author Francesco Bertolaccini 543981
 * 
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 * 
 * \brief Buffer circolare concorrente
 */

#ifndef _CCIRCBUF_H
#define _CCIRCBUF_H

#include <sys/types.h>

/// Buffer circolare concorrente
typedef struct ccircbuf ccircbuf_t;

/**
 * \brief Inizializza il buffer circolare
 * 
 * \param len Capienza del buffer
 * \return ccircbuf_t* Il buffer inizializzato. Se NULL, errno è impostato
 */
ccircbuf_t *ccircbuf_init(size_t len);

/**
 * \brief Distrugge il buffer circolare
 * 
 * \param buf Il buffer da distruggere
 * \return int 0 in caso di successo, -1 e errno impostato in caso di errori
 */
int ccircbuf_deinit(ccircbuf_t *buf);

/**
 * \brief Estrae tutti gli elementi presenti nel buffer circolare
 * 
 * \param buf Il buffer da cui estrarre gli elementi
 * \param dest Puntatore dove porre gli elementi estratti
 * \return int -1 ed errno impostato in caso di errori, il numero di elementi estratti in caso di successo
 */
int ccircbuf_get_elems(ccircbuf_t* buf, void ***dest);

/**
 * \brief Inserisce un elemento nel buffer
 * 
 * \param buf Il buffer in cui inserire l'elemento
 * \param elem L'elemento da inserire
 * \param oldElem In caso di sorpasso della capienza, l'elemento che è stato sovrascritto
 * \return int 0 in caso di successo, -1 ed errno impostato in caso di errori
 */
int ccircbuf_insert(ccircbuf_t* buf, void *elem, void **oldElem);

#endif /* _CCIRCBUF_H */