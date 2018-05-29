/*
 * membox Progetto del corso di LSO 2017/2018
 *
 * Dipartimento di Informatica Università di Pisa
 * Docenti: Prencipe, Torquati
 * 
 */
/**
 * \brief File contenente alcune define con valori massimi utilizzabili
 */

#if !defined(CONFIG_H_)
#define CONFIG_H_

/// Massima lunghezza di un nickname
#define MAX_NAME_LENGTH 32

/// Massima lunghezza di un path nei file di configurazione
#define MAX_PATH_LEN 128

/// Se 0, OP_USRLIST invierà la lista degli utenti _registrati_, se 1 quella degli utenti _online_
#define SEND_ONLINE_USERS 1

// to avoid warnings like "ISO C forbids an empty translation unit"
typedef int make_iso_compilers_happy;

#endif /* CONFIG_H_ */
