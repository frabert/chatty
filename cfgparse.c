/**
 *  \file cfgparse.c
 *  \author Francesco Bertolaccini 543981
 * 
 *  Si dichiara che il contenuto di questo file e' in ogni sua parte opera
 *    originale dell'autore
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "cfgparse.h"

/**
 * \brief Ignora una sequenza di caratteri vuoti. Se trovata, restituisce 1, altrimenti 0
 */
int skipWhitespace(char **str);

/**
 * \brief Ignora un commento. Se trovato, restituisce 1, altrimenti 0
 */
int skipComment(char **str);

/**
 * \brief Supera un carattere newline. Se trovato restituisce 1, altrimenti 0
 */
int parseEOL(char **str);

/**
 * \brief Restituisce 1 se alla fine della stringa. Altrimenti 0
 */
int parseEOF(char **str);

/**
 * \brief Legge un identificatore e lo restituisce. Se non viene trovato, restituisce NULL
 */
char *parseIdent(char **str);

/**
 * \brief Legge un valore e lo restituisce. Se non viene trovato, restituisce NULL
 */
char *parseValue(char **str);

/**
 * \brief Legge una stringa e la restituisce. Se non viene trovata, restituisce NULL
 */
int parseString(char **str, char **out);

int cfg_parse(char *str, ConfigCallback cb, void *userdata) {
  int count = 0;
  while(*str != '\0') {
    skipWhitespace(&str); /* Ignora gli whitespace a inizio riga */
    if(skipComment(&str)) {
      /* Se c'è un commento, lo ignora e salta alla riga successiva */
      parseEOL(&str);
      continue;
    }

    if (parseEOL(&str)) {
      /* Se la riga è vuota, la salta */
      continue;
    }

    if (parseEOF(&str)) {
        /* Se il file è terminato, esci */
        break;
    }

    /* Legge l'identificatore a cui assegnare il valore */
    char *ident = parseIdent(&str);

    if(!ident) {
        errno = EINVAL;
        return -1;
    }

    /* Salta ogni whitespace che ci potrebbe essere dopo l'identificatore */
    skipWhitespace(&str);
    if(*str != '=') {
      free(ident);
      errno = EINVAL;
      return -1;
    }
    str = str + 1;
    /* Salta ogni whitespace dopo l'uguale */
    skipWhitespace(&str);
    char *value = parseValue(&str);

    if(!value || !cb(ident, value, userdata)) {
      /* Se non è stato trovato un valore o la callback fallisce, termina */
      free(ident);
      free(value);
      return -1;
    } else {
      count++;
      free(ident);
      free(value);
    }
  }
  return count;
}

int skipWhitespace(char **str) {
  if(!(**str != '\0' && **str < 33 && **str != '\n')) {
    return 0;
  }

  while(**str != '\0' && **str < 33 && **str != '\n') {
    *str = *str + 1;
  }

  return 1;
}

int skipComment(char **str) {
  if(**str == '#') {
    while(**str && **str != '\n') {
      *str = *str + 1;
    }
    return 1;
  } else {
    return 0;
  }
}

char *parseIdent(char **str) {
  /* Un identificatore deve iniziare con una lettera maiuscola o minuscola */
  if((**str >= 'a' && **str <= 'z') ||
     (**str >= 'A' && **str <= 'Z')) {

    int len = 0;
    char *start = *str;

    /* I caratteri successivi al primo possono anche essere cifre */
    while((**str >= '0' && **str <= '9') ||
          (**str >= 'a' && **str <= 'z') ||
          (**str >= 'A' && **str <= 'Z')) {
      *str = *str + 1;
      len++;
    }
    char *res = (char *)calloc(len + 1, sizeof(char));
    memcpy(res, start, sizeof(char) * len);
    res[len] = '\0';

    return res;
  } else {
    return NULL;
  }
}

int parseString(char **str, char **out) {
  if(**str == '"') {
    *str = *str + 1;
    char *start = *str;
    int len = 0;
    /* Accumula caratteri fino alle virgolette successive */
    while(**str != '"') {
      if(**str == '\0' || **str == '\n') {
        /* Errore sintattico se la stringa non è stata chiusa
          prima della fine della linea / file */ 
        return -1;
      }

      len++;
      *str = *str + 1;
    }
    *str = *str + 1;

    *out = calloc(len + 1, sizeof(char));
    memcpy(*out, start, sizeof(char) * len);
    (*out)[len] = '\0';
    return 1;
  } else {
    if (**str != '\0' && **str >= 33) {
      int len = 0;
      char *start = *str;
      /* Accumula tutti i caratteri non vuoti */
      while(**str != '\0' && **str >= 33) {
        len++;
        *str = *str + 1;
      }

      *out = calloc(len + 1, sizeof(char));
      memcpy(*out, start, sizeof(char) * len);
      (*out)[len] = '\0';
      return 1;
    } else {
      /* Non è stata trovata la stringa */
      return 0;
    }
  }
}

int parseEOL(char **str) {
  if(**str == '\n') {
    *str = *str + 1;
    return 1;
  }
  else return 0;
}

int parseEOF(char **str) {
  if(**str == '\0') {
    return 1;
  }
  else return 0;
}

char *parseValue(char **str) {
  char *value;
  if(!parseString(str, &value)) {
    return NULL;
  }

  skipWhitespace(str);
  skipComment(str);
  if(!parseEOL(str) && !parseEOF(str)) {
    free(value);
    return NULL;
  }

  return value;
}