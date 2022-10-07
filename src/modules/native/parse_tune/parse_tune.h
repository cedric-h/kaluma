#include "tone_map.h"

#define ARR_LEN(arr) (sizeof(arr) / sizeof(arr[0]))
typedef struct {
  uint8_t open;

  float to_wait;

  char *str_complete;
  char *str; /* within *str_complete and '\0' */
} NoteReadState;

static uint8_t note_read(NoteReadState *nrs) {
  char *endptr = NULL;

  /* if (*nrs->str == 0) return 0; */

  if (!nrs->open) {
    nrs->to_wait = strtof(nrs->str, &endptr);
    if (endptr) {
      nrs->str = endptr;

      /* if there's more than just a pause, we open for notes */
      if (*nrs->str == ':') {
        nrs->str++; /* consume the colon! */
        nrs->open = 1;
        return 1;
      }

      /* if it's a comma and newline we eat that shit (and stay closed) */
      if (*nrs->str == ',') {
        nrs->str += 2;
        return 1; /* where there's a comma in this spec, there's more */
      }

      #if 0
      this and a line up at the top of this function are commented out,
      because i think ignoring the final duration is something we actually
      want to do!
      if (*nrs->str == '\0') {
        /* this is fucked up because 1 has meant "there's more" but */
        return 1;
      }
      #endif

      /* just filter out empty lines between notes i guess */
      if (*nrs->str == '\n') nrs->str++;
    }
  }
  else {
    while (1) {
      if (*nrs->str == ' ') { nrs->str++; continue; }
      if (*nrs->str == '+') { nrs->str++; continue; }
      if (*nrs->str ==   0) { nrs->open = 0; break; }
      if (*nrs->str <= 'z' && *nrs->str >= 'a') {

        char note[4] = {0};
        int freq = 0;
        {
          int note_len = 0;
          while (isalnum((int)*nrs->str) || *nrs->str == '#')
            note[note_len++] = *nrs->str++;
          freq = tone_map[fnv1_hash(note, note_len) % ARR_LEN(tone_map)];
        }

        char shape = *nrs->str++;

        endptr = NULL;
        float duration = strtof(nrs->str, &endptr);
        if (endptr) nrs->str = endptr;

        printf("sound { note: %s, freq: %d, shape: %c, duration: %f }\n",
          note, freq, shape, duration);

        /* go ahead and eat a comma + close, if we can */
        if (*nrs->str == ',') {
          nrs->open = 0;

          /* we eat commas and newlines */
          nrs->str += 2;
        }

        return 1;
      }
      break;
    }
  }

  return 0;
}

static uint8_t tune_parse(NoteReadState *nrs) {
  if (note_read(nrs)) {
    if (!nrs->open) printf("pause { duration: %fms }\n", nrs->to_wait);
    return 1;
  }
  if (*nrs->str == '\0') return 0;

  return 1;
}
