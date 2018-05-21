/** \file cqueue.h
 *  \author Francesco Bertolaccini 543981
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 * 
 * \brief Coda concorrente
 * Consente di gestire una coda FIFO fra più thread
 * 
 * Implementata tramite una linked list
 */

#ifndef CQUEUE_H
#define CQUEUE_H

typedef struct cqueue cqueue_t;

/**
 * \brief Tipo della funzione di callback che dealloca elementi dalla coda
 * 
 * Viene chiamata quando la coda non è vuota e viene deallocata
 */
typedef void(cqueue_deinitializer)(void *elem);

/**
 * \brief Inizializza la coda concorrente
 * 
 * \return cqueue_t* NULL se l'inizializzazione non ha avuto successo. Se si sono verificati errori viene impostato errno.
 */
cqueue_t *cqueue_init();

/**
 * \brief Dealloca la coda
 * 
 * \param queue La coda da deallocare
 * \param cb La funzione da chiamare per ogni elemento ancora presente sulla coda. Può essere NULL.
 * \return int 0 se la funzione ha avuto successo, -1 altrimenti. Viene impostato errno se si sono verificati errori.
 */
int cqueue_deinit(cqueue_t *queue, cqueue_deinitializer cb);

/**
 * \brief Inserisce un elemento nella coda
 * 
 * \param cq La coda in cui inserire l'elemento
 * \param v L'elemento da inserire nella coda
 * \return int 0 se la funzione ha avuto successo, -1 altrimenti. Viene impostato errno se si sono verificati errori.
 */
int cqueue_push(cqueue_t *cq, void *v);

/**
 * \brief Estrae un elemento dalla coda.
 * Se la coda è vuota, attende che venga inserito un elemento.
 * 
 * \param cq La coda da cui estrarre
 * \param elem L'elemento estratto
 * \return int 0 se la funzione ha avuto successo, -1 altrimenti. Viene impostato errno se si sono verificati errori.
 */
int cqueue_pop(cqueue_t *cq, void **elem);

/**
 * \brief Ottiene la lunghezza della code
 * 
 * \param cq La coda di cui ottenere la lunghezza
 * 
 * \return int >=0 se la chiamata ha avuto successo, -1 altrimenti. Viene impostato errno se si sono verificati errori.
 */
int cqueue_size(cqueue_t *cq);

#endif /* CQUEUE_H */