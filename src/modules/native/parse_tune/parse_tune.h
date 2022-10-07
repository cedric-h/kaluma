#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "tone_map.h"

#define ARR_LEN(arr) (sizeof(arr) / sizeof(arr[0]))
typedef struct {
  uint8_t open;
  char *str;
} NoteReadState;

static uint8_t note_read(NoteReadState *nrs) {
  char *endptr = NULL;

  if (!nrs->open) {
    printf("pause { duration: %fms }\n", strtof(nrs->str, &endptr));
    if (endptr) {
      nrs->str = endptr;

      /* if there's more than just a pause, we open for notes */
      if (*nrs->str == ':') {
        nrs->str++; /* consume the comma! */
        nrs->open = 1;
        return 1;
      }
    }
  }
  else {
    while (1) {
      if (*nrs->str == ' ') { nrs->str++; continue; }
      if (*nrs->str == '+') { nrs->str++; continue; }
      if (*nrs->str == ',') { nrs->open = 0; break; }
      if (*nrs->str ==   0) { nrs->open = 0; break; }
      if (*nrs->str <= 'z' && *nrs->str >= 'a') {

        char note[4] = {0};
        int freq = 0;
        {
          int note_len = 0;
          while (isalnum(*nrs->str) || *nrs->str == '#')
            note[note_len++] = *nrs->str++;
          freq = tone_map[fnv1_hash(note, note_len) % ARR_LEN(tone_map)];
        }

        char shape = *nrs->str++;

        endptr = NULL;
        float duration = strtof(nrs->str, &endptr);
        if (endptr) nrs->str = endptr;

        printf("sound { note: %s, freq: %d, shape: %c, duration: %f }\n",
          note, freq, shape, duration);

        return 1;
      }
      break;
    }
  }

  return 0;
}

static uint8_t tune_parse(NoteReadState *nrs) {
  if (note_read(nrs)) return 1;

  if (!nrs->open) {
    /* if the note didn't end with a comma, your tune is done! */
    if (*nrs->str != ',') return 0;
    /* eat that comma and newline!!! */
    nrs->str += 2;
  }

  return 1;
}
