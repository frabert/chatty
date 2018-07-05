/**
 *  \file cstrlist.h
 *  \author Francesco Bertolaccini 543981
 * 
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 * 
 * \brief List di stringhe concorrente
 * Consente di gestire una lista di stringhe fra più thread
 * 
 * Implementata con liste linkate
 */

#ifndef CSTRLIST_H
#define CSTRLIST_H

/// Lista di stringhe concorrente
typedef struct cstrlist cstrlist_t;

/**
 * \brief Inizializza una nuova lista di stringhe concorrente
 * 
 * \return cstrlist_t* NULL e errno impostato in caso di errori
 */
cstrlist_t *cstrlist_init();

/**
 * \brief Distrugge una lista
 * 
 * \param list La lista da distruggere
 * \return int 0 in caso di successo, -1 e errno impostato in caso di errori
 */
int cstrlist_deinit(cstrlist_t *list);

/**
 * \brief Inserisce una nuova stringa nella lista
 * 
 * Se il valore specificato era già presente nella lista, la chiamata fallisce
 * 
 * \param list La lista in cui inserire il valore
 * \param v Il valore da inserire nella lista
 * \return int 0 in caso di successo, -1 e errno impostato in caso di errore
 */
int cstrlist_insert(cstrlist_t *list, const char *v);

/**
 * \brief Rimuove una stringa dalla lista
 * 
 * \param list La lista da cui rimuovere il valore
 * \param v Il valore da rimuovere
 * \return int 0 in caso di successo, -1 e errno impostato in caso di errore
 */
int cstrlist_remove(cstrlist_t *list, const char *v);

/**
 * \brief Consente di ottenere i valori attualmente contenuti nella lista
 * 
 * \param list La lista da cui estrarre i valori
 * \param dest Puntatore ad un array di stringhe
 * \return int >=0 in caso di successo, -1 e errno impostato in caso di errore
 */
int cstrlist_get_values(cstrlist_t *list, char ***dest);

#endif /* CSTRLIST_H */