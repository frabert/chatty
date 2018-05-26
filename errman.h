/**
 *  \author Francesco Bertolaccini 543981
 * 
 *   Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *     originale dell'autore
 *  \brief Varie macro per la gestione degli errori
 */

#ifndef ERRMAN_H_
#define ERRMAN_H_

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

/**
 * \brief Se x vale < 0, il programma termina con un messaggio d'errore
 * 
 * Il messaggio d'errore conterrà anche un riferimento a dove è stato generato
 */
#define HANDLE_FATAL(x, s) if((x) < 0) { \
                            fprintf(stderr, "%s:%d ", __FILE__, __LINE__); \
                            perror(s); \
                            exit(EXIT_FAILURE); }

/**
 * \brief Se x è NULL, il programma termina con un messaggio d'errore
 * 
 * Il messaggio d'errore conterrà anche un riferimento a dove è stato generato
 */
#define HANDLE_NULL(x, s) if ((x) == NULL) { \
                          fprintf(stderr, "%s: %d ", __FILE__, __LINE__); \
                          perror(s); \
                          exit(EXIT_FAILURE); }

#endif /* ERRMAN_H_ */