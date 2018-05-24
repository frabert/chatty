/** \file chash.h
 *  \author Francesco Bertolaccini 543981
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 * 
 * \brief Hashtable concorrente
 * 
 * L'hashtable è implementata tramite liste di trabocco.
 * L'accesso concorrente alla tabella è consentito nel caso in cui si tenti
 * di leggere le chiavi presenti (o il loro numero) o si acceda ad elementi
 * con chiavi separate. La scrittura della tabella è bloccante.
 * La funzione di hash è stata presa da http://www.cse.yorku.ca/~oz/hash.html
 */

#ifndef CHASH_H_
#define CHASH_H_

typedef struct chash chash_t;

/**
 * \brief Tipo della funzione di callback che dealloca elementi dalla hashtable
 * 
 * Viene chiamata quando la hashtable non è vuota e viene deallocata
 */
typedef void(chash_deinitializer)(void *elem);

/**
 * \brief Tipo della funzione di callback che viene chiamata quando
 *        si estraggono elementi dalla hashtable
 * 
 * \param key La chiave della entry estratta
 * \param value Il valore estratto dalla callback
 * \param ud Dati arbitrari passati dall'utente
 */
typedef void(chash_get_callback)(const char *key, void *value, void *ud);

/**
 * \brief Inizializza una hashtable concorrente
 * 
 * \return chash_t* Viene restituito NULL e errno viene impostato se la funzione fallisce 
 */
chash_t *chash_init();

/**
 * \brief Deinizializza una hashtable concorrente
 * 
 * \param table La hashtable da deinizializzare
 * \param cb La callback da chiamare su ogni elemento ancora presente nella hastable al momento della distruzione
 * \return int 0 se non si sono verificati errori, -1 altrimenti. Viene impostato errno se si è verificato un errore.
 */
int chash_deinit(chash_t *table, chash_deinitializer cb);

/**
 * \brief Ottiene un valore dalla hashtable
 * 
 * \param table La hashtable da cui ottenere il valore
 * \param key La chiave del valore da ottenere
 * \param cb La funzione che viene chiamata una volta che l'elemento è stato ottenuto. Non può essere NULL.
 *           ATTENZIONE: un tentativo di accesso allo stesso elemento della tabella da questa callback causa deadlock
 * \param ud Un valore scelto dall'utente da passare alla callback
 * \return int 0 se non si sono verificati errori, -1 altrimenti. Viene impostato errno se si è verificato un errore
 */
int chash_get(chash_t *table, const char *key, chash_get_callback cb, void *ud);

/**
 * \brief Ottiene tutti i valori presenti nella hashtable
 * 
 * \param table La hashtable da scandire
 * \param cb La callback da chiamare su ogni valore presente.
 *           ATTENZIONE: non è possibile accedere ulteriormente alla stessa hashtable dall'interno di questa callback
 * \param ud Un valore scelto dall'utente da passare alla callback
 * \return int 0 se non si sono verificati errori, -1 altrimenti. Viene impostato errno se si è verificato un errore
 */
int chash_get_all(chash_t *table, chash_get_callback cb, void *ud);

/**
 * \brief Imposta un valore nella hashtable
 * 
 * \param table La hashtable in cui impostare il valore
 * \param key La chiave a cui impostare il valore
 * \param value Il valore da impostare. Se NULL, viene eliminato il valore presente nella hashtable
 * \param oldValue Il vecchio valore presente con la stessa chiave
 * \return int 0 se non si sono verificati errori, -1 altrimenti. Viene impostato errno se si è verificato un errore
 */
int chash_set(chash_t *table, const char *key, void *value, void **oldValue);

/**
 * \brief Consente di ottenere tutte le chiavi presenti nella tabella
 * 
 * \param table La tabella da cui estrarre le chiavi
 * \param keys Variabile dove mettere le chiavi estratte
 * 
 * \remark Il vettore di stringhe e le stringhe in esso contenute dovranno essere
 *         liberate manualmente
 * 
 * \return int Il numero di chiavi estratte o -1 se si sono verificati errori. Nel caso, errno viene impostato
 */
int chash_keys(chash_t *table, char **keys);

#endif /* CHASH_H_ */