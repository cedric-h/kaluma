/* Copyright (c) 2017 Pico
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"

#include "jerryscript.h"
#include "jerryxx.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include "i2saudio_magic_strings.h"

#define I2S_CLK_PINS 10
#define I2S_DATA_PIN 9

typedef enum {
  Sound_Sine,
  Sound_Square,
  Sound_Triangle,
  Sound_Sawtooth,
  Sound_COUNT,
} Sound;

static uint32_t freq_to_delta(float freq) { return freq/16000.0*256.0*65536.0; }

uint32_t ticks = 0;

typedef struct {
  uint32_t phase, delta;
  uint16_t volume;
  Sound sound;
} Channel;

#define FIFO_LENGTH (1 << 8)
queue_t message_fifo;
typedef enum {
  MessageKind_PushFreq,
  MessageKind_Wait,
} MessageKind;
typedef struct {
  MessageKind kind;
  unsigned int duration; /* for MessageKind_Wait */
  float freq;  /* for MessageKind_PushFreq */
  Sound sound; /* for MessageKind_PushFreq */
} Message;

#define CHANNEL_COUNT (32)
static struct {
  Channel channels[CHANNEL_COUNT];
} state;

#define SOUND_SAMPLE_LEN (1 << 9)
typedef struct { uint16_t data[SOUND_SAMPLE_LEN]; } SoundSample;
static SoundSample sound_samples[Sound_COUNT];
#define MAX_PHASE (0xffffff)

void pio0_irq_0_handler(void) {
  uint32_t ints = pio0->ints0;
  uint16_t sample = 0x0000;

  ints >>= 8;
  pio0->irq = ints;

  for (int i = 0; i < CHANNEL_COUNT; i++) {
    Channel *chan = state.channels + i;

    if (chan->volume)
      sample += sound_samples[chan->sound].data[chan->phase / (MAX_PHASE/SOUND_SAMPLE_LEN)];
  }

  pio_sm_put(pio0, 0, sample<<16);

  for (int i = 0; i < CHANNEL_COUNT; i++) {
    Channel *chan = state.channels + i;

    chan->phase += chan->delta;
    chan->phase &= 0xffffff;
  }
  
  ticks++;
}

/*
.wrap_target
  set x 14      side 0b01
left:
  out pins 1    side 0b00
  jmp x-- left  side 0b01
  out pins 1    side 0b10  
  set x 14      side 0b11
right:
  out pins 1    side 0b10  
  jmp x-- right side 0b11
  out pins 1    side 0b00  
.wrap
*/

uint16_t pio_instructions_write_16[] = {0xe82e, 0x6001, 0x0841, 0x7001, 0xf82e, 0x7001, 0x1845, 0x6001};

pio_program_t pio_write_16 = {
    pio_instructions_write_16,
    sizeof(pio_instructions_write_16) / sizeof(uint16_t),
    -1
};

// JERRYXX_FUN(i2saudio_ticks_fn) { return jerry_create_number(ticks); }

JERRYXX_FUN(i2saudio_wait_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "time");
  uint16_t v = jerry_value_as_uint32(JERRYXX_GET_ARG(0));

  Message msg = { .kind = MessageKind_Wait, .duration = v };

  /* returns true if you need to wait
   * if we were able to add your thing to the fifo, we don't need to wait.
   * queue_try_add returns true if your thing was added to the fifo.
   * therefore, you need to wait if queue_try_add doesn't return true */
  return jerry_create_boolean(!queue_try_add(&message_fifo, &msg));
}

JERRYXX_FUN(i2saudio_push_freq_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "freq");
  uint16_t v = jerry_value_as_uint32(JERRYXX_GET_ARG(0));
  Sound sound = JERRYXX_GET_ARG_NUMBER(1);

  Message msg = { .kind = MessageKind_PushFreq, .sound = sound, .freq = v };

  /* returns true if you need to wait
   * if we were able to add your thing to the fifo, we don't need to wait.
   * queue_try_add returns true if your thing was added to the fifo.
   * therefore, you need to wait if queue_try_add doesn't return true */
  return jerry_create_boolean(!queue_try_add(&message_fifo, &msg));
}

// JERRYXX_FUN(i2saudio_setvolume1_fn) {
//   JERRYXX_CHECK_ARG_NUMBER(0, "setvolume1");
//   volume1 = (uint16_t)jerry_value_as_uint32(JERRYXX_GET_ARG(0));
//   return jerry_create_undefined();
// }
// 
// JERRYXX_FUN(i2saudio_setfreq1_fn) {
//   JERRYXX_CHECK_ARG_NUMBER(0, "setfreq1");
//   double freq = jerry_get_number_value(JERRYXX_GET_ARG(0));
//   double ddelta = freq/16000.0*256.0*65536.0;
//   delta1 = (uint32_t) ddelta;
//   return jerry_create_undefined();
// }

JERRYXX_FUN(i2saudio_midi_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "midi");
  int32_t n = jerry_value_as_int32(JERRYXX_GET_ARG(0));
  double freq = 130.8128*pow(1.0594630943592953,n-48);
  return jerry_create_number(freq);
}

/* our input handling is a bit onerous, but unlike simpler solutions ... it works.
 * you see, input polling had too many missed keypresses (go figure!)
 * but IRQ, the canonical solution, had too many false positives
 * (wouldn't be surprised if our unsoldered buttons had something to do with this?)
 *
 * so in order to filter out the fake keypresses IRQ was registering, we now have
 * our audio thread poll for those and use the timing between them to filter out
 * fake ones (if the button is being held down for 5ms, that's probably just noise!)
 * 
 * so there's a lot of message passing here, unfortunately.
 * IRQ has to pass messages to the audio thread, which has to pass messages
 * to the main thread running your kaluma code. yikes!
 *
 * this is done by way of two FIFOs. you can imagine one as being "lower level"
 * than the other. the lower level one would be button_fifo; that has all of the
 * events from IRQ, some of which will be false positives. 
 *
 *                IRQ -> AUDIO THREAD -> MAIN THREAD
 *     the first arrow is button_fifo, the second is press_fifo!
 *
 */

#define PRESS_FIFO_LENGTH (32)
queue_t press_fifo;
typedef struct { uint8_t pin; } PressAction;

#define BUTTON_FIFO_LENGTH (32)
queue_t button_fifo;
typedef enum {
  ButtonActionKind_Down,
  ButtonActionKind_Up,
} ButtonActionKind;
typedef struct {
  ButtonActionKind kind;
  absolute_time_t time;
  uint8_t pindex;
} ButtonAction;

typedef struct {
  absolute_time_t last_up, last_down;
  uint8_t edge;
} ButtonState;

#define ARR_LEN(x) (sizeof(x) / sizeof(x[0]))
uint button_pins[] = {  5,  7,  6,  8, 12, 14, 13, 15 };

void gpio_callback(uint gpio, uint32_t events) {
  for (int i = 0; i < ARR_LEN(button_pins); i++)
    if (button_pins[i] == gpio) {

      puts("is it possible? conceivable?");
      queue_add_blocking(&button_fifo, &(ButtonAction) {
        .time = get_absolute_time(),
        .kind = (events == GPIO_IRQ_EDGE_RISE) ? ButtonActionKind_Up : ButtonActionKind_Down,
        .pindex = i,
      });

      break;
    }
}

static ButtonState button_states[ARR_LEN(button_pins)] = {0};
static void button_init(void) {
  queue_init(&button_fifo, sizeof(ButtonAction), BUTTON_FIFO_LENGTH);
  queue_init(& press_fifo, sizeof(PressAction ),  PRESS_FIFO_LENGTH);

  for (int i = 0; i < ARR_LEN(button_pins); i++) {
    ButtonState *bs = button_states + i;
    bs->edge = 1;
    bs->last_up = bs->last_down = get_absolute_time();

    gpio_set_dir(button_pins[i], GPIO_IN);
    gpio_pull_up(button_pins[i]);
    // gpio_set_input_hysteresis_enabled(button_pins[i], 1);
    // gpio_set_slew_rate(button_pins[i], GPIO_SLEW_RATE_SLOW);
    // gpio_disable_pulls(button_pins[i]);

    gpio_set_irq_enabled_with_callback(
      button_pins[i],
      GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
      true,
      &gpio_callback
    );
  }
}

static void button_poll(void) {
  ButtonAction p;
  if (queue_try_remove(&button_fifo, &p)) {
    ButtonState *bs = button_states + p.pindex;
    switch (p.kind) {
      case ButtonActionKind_Down: bs->last_down = p.time; break;
      case ButtonActionKind_Up:   bs->last_up   = p.time; break;
    }
  }

  for (int i = 0; i < ARR_LEN(button_pins); i++) {
    ButtonState *bs = button_states + i;

    absolute_time_t when_up_cooldown = delayed_by_ms(bs->last_up  , 70);
    absolute_time_t light_when_start = delayed_by_ms(bs->last_down, 20);
    absolute_time_t light_when_stop  = delayed_by_ms(bs->last_down, 40);

    uint8_t on = absolute_time_diff_us(get_absolute_time(), light_when_start) < 0 &&
                 absolute_time_diff_us(get_absolute_time(), light_when_stop ) > 0 &&
                 absolute_time_diff_us(get_absolute_time(), when_up_cooldown) < 0  ;

    // if (on) puts("BRUH");

    if (!on && !bs->edge) bs->edge = 1;
    if (on && bs->edge) {
      bs->edge = 0;

      if (!queue_try_add(&press_fifo, &(PressAction) { .pin = button_pins[i] }))
        puts("press_fifo full!");
    }
  }
}

JERRYXX_FUN(i2saudio_press_queue_try_remove) {
  PressAction p;
  if (!queue_try_remove(&press_fifo, &p))
    return jerry_create_undefined();

  return jerry_create_number(p.pin);
}

// JERRYXX_FUN(button_check) { return jerry_create_undefined(); }

void core1_entry(void) {
  pio_sm_set_enabled(pio0, 0, false);
  pio_gpio_init(pio0, I2S_CLK_PINS);
  pio_gpio_init(pio0, I2S_CLK_PINS+1);
  pio_gpio_init(pio0, I2S_DATA_PIN);
  pio_add_program_at_offset(pio0, &pio_write_16,0);
  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_out_pins(&c, I2S_DATA_PIN, 1);
  sm_config_set_sideset_pins(&c, I2S_CLK_PINS);
  sm_config_set_sideset(&c, 2, false, false);
  sm_config_set_wrap(&c, 0, pio_write_16.length-1);
  sm_config_set_out_shift(&c, false, true, 32); 
  sm_config_set_clkdiv(&c, 122.07);  // 125000000 / 16000 / 32 / 2 
  pio_sm_set_consecutive_pindirs(pio0, 0, I2S_CLK_PINS, 2, true);
  pio_sm_set_consecutive_pindirs(pio0, 0, I2S_DATA_PIN, 1, true);
  pio_sm_init(pio0, 0, 0, &c);
  pio_sm_set_enabled(pio0, 0, true);

  irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq_0_handler);
  irq_set_enabled(PIO0_IRQ_0, true);

  pio0->inte0 |= PIO_INTR_SM0_TXNFULL_BITS;

  button_init();

  #define VOL (15000)
  for (int i = 0; i < SOUND_SAMPLE_LEN; i++) {
    float t = (float)i / (float)SOUND_SAMPLE_LEN;

    t = fmodf(t * 2.0f, 1.0f);
    sound_samples[Sound_Sine    ].data[i] = (0.5 + 0.5 * cosf(t * M_PI * 2)) * VOL * 2.1;
    sound_samples[Sound_Triangle].data[i] = 2.0f*fabsf(0.5f - t) * VOL;
    sound_samples[Sound_Square  ].data[i] = (t > 0.5f) ? (VOL*0.3f) : 0;
    sound_samples[Sound_Sawtooth].data[i] = t * VOL * 0.3f;

    // sound_samples[Sound_Sine    ].data[i] = (0.5f + 0.5f*sinf(t * M_PI * 2)) * (float)VOL;
    // sound_samples[Sound_Square  ].data[i] = VOL + ((t > 0.5f) ? VOL : -VOL);
    // printf("sound_samples[Sound_Sine].data[i]=%d\n", sound_samples[Sound_Sine].data[i]);
    // printf("sound_samples[Sound_Square].data[i]=%d\n", sound_samples[Sound_Square].data[i]);
    // sound_samples[Sound_Triangle].data[i] = 2.0f*fabsf(0.5f - t) * VOL;
    // sound_samples[Sound_Sawtooth].data[i] = fmodf(t, 0.5f)*2.0f * VOL;
  }

  absolute_time_t sound_finish = get_absolute_time();

  Channel *next_chan = state.channels;
  /* used to track rising edge of note_done */
  uint8_t playing_note = 0;

  Message message;
  while (true) {
    int until_finish = absolute_time_diff_us(get_absolute_time(), sound_finish);
    /* if you have to go backwards to get to the finish ... lmao */
    uint8_t note_done = until_finish < 0;

    if (note_done) {
      if (playing_note) {
        playing_note = 0;

        next_chan = state.channels;

        /* turn 'em off and wait for more data */
        for (int i = 0; i < CHANNEL_COUNT; i++)
          state.channels[i].volume = 0;
      }

      /* fill up the channels with our note data */
      while (queue_try_remove(&message_fifo, &message)) {
        if (message.kind == MessageKind_PushFreq) {
          switch (message.sound) {
          case Sound_Sine    : puts("Sound_Sine    "); break;
          case Sound_Square  : puts("Sound_Square  "); break;
          case Sound_Triangle: puts("Sound_Triangle"); break;
          case Sound_Sawtooth: puts("Sound_Sawtooth"); break;
          default: puts("Sound_Wtf"); break;
          }

          if ((next_chan - state.channels) >= CHANNEL_COUNT) {
            puts("ran out of audio channels somehow");
          } else {
            Channel *chan = next_chan++;
            chan->volume = 1500;
            chan->delta = freq_to_delta(message.freq);
            chan->sound = message.sound;
          }
        } else if (message.kind == MessageKind_Wait) {
          sound_finish = delayed_by_ms(get_absolute_time(), message.duration);
          playing_note = 1;
          break;
        }
      }
    }

    button_poll();
  }
}

jerry_value_t module_i2saudio_init(void) {

  puts("launching audio core");
  queue_init(&message_fifo, sizeof(Message), FIFO_LENGTH);
  multicore_reset_core1();
  multicore_launch_core1(core1_entry);

  jerry_value_t exports = jerry_create_object();
  // jerryxx_set_property_function(exports, MSTR_I2SAUDIO_TICKS, i2saudio_ticks_fn);

  jerryxx_set_property_function(exports, MSTR_I2SAUDIO_WAIT, i2saudio_wait_fn);
  jerryxx_set_property_function(exports, MSTR_I2SAUDIO_PUSH_FREQ, i2saudio_push_freq_fn);

  jerryxx_set_property_function(exports, MSTR_I2SAUDIO_PRESS_QUEUE_TRY_REMOVE, i2saudio_press_queue_try_remove);

  jerryxx_set_property_function(exports, MSTR_I2SAUDIO_MIDI, i2saudio_midi_fn);
  return exports;
}
