/** \file cfgparse.h
 *  \author Francesco Bertolaccini 543981
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 *  \brief Utilità di parsing dei file di configurazione
 * 
 * Effettua il parsing di un file di configurazione e chiama una funzione
 * fornita dall'utente con il nome del campo e il valore associato, oltre
 * ad un terzo valore fornito opzionalmente dall'utente
 */

#ifndef CFGPARSE_H_
#define CFGPARSE_H_

/**
 * \typedef ConfigCallback
 * \brief Tipo della funzione di callback che viene chiamata
 *        ad ogni valore di configurazione trovato
 * 
 * \param key Il nome del campo il cui valore è stato impostato.
 *            NB: NON MEMORIZZARE QUESTO VALORE!
 *            IL PUNTATORE VERRA' DISTRUTTO AL TERMINE DELLA CHIAMATA!
 * 
 * \param value Il valore da assegnare al campo
 *            NB: NON MEMORIZZARE QUESTO VALORE!
 *            IL PUNTATORE VERRA' DISTRUTTO AL TERMINE DELLA CHIAMATA!
 * \param ud Un puntatore fornito dall'utente all'inizio del parsing
 * \return 1 se il parsing deve continuare, 0 se è stato incontrato un errore
 *         e il parsing deve terminare
 */
typedef int (*ConfigCallback)(const char *key, const char *value, void *ud);

/**
 * \fn cfg_parse
 * 
 * \brief Funzione principale di parsing
 * 
 * \param str La stringa null-terminata su cui effettuare il parsing
 * \param cb La funzione che viene chiamata dopo aver letto un valore
 * \param userdata Un valore opzionale che verrà passato alla callback
 * \return Il numero di impostazioni lette, -1 se è stato riscontrato un errore
 */
int cfg_parse(char *str, ConfigCallback cb, void *userdata);

#endif /* CFGPARSE_H_ */