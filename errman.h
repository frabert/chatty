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
 * \brief Restituisce true se x è un errore relativo alla disconnessione
 * di un client
 */
#define HAS_DISCONNECTED(x) ((x) < 0 && (errno == EPIPE \
                                      || errno == ECONNABORTED \
                                      || errno == ECONNRESET \
                                      || errno == ECONNREFUSED \
                                      || errno == EBADF))

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

#ifdef CHATTY_VERBOSE
#  define  LOG_ERR(format, ...) fprintf(stderr, "%s:%d - \033[0;31m" format "\033[0m\n", __FILE__, __LINE__, __VA_ARGS__)
#  define LOG_INFO(format, ...) fprintf(stdout, "%s:%d - \033[0;32m" format "\033[0m\n", __FILE__, __LINE__, __VA_ARGS__)
#  define LOG_WARN(format, ...) fprintf(stdout, "%s:%d - \033[0;33m" format "\033[0m\n", __FILE__, __LINE__, __VA_ARGS__)
#else
#  define  LOG_ERR(format, ...)
#  define LOG_INFO(format, ...)
#  define LOG_WARN(format, ...)
#endif

#endif /* ERRMAN_H_ */