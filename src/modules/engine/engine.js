const { GPIO } = require("gpio");
const { screen } = require("screen");
const gc = screen.getContext("buffer");
const native = require("native");
const audio = require("i2saudio");

/* re-exports from C; bottom of src/modules/native/module_native.c has notes about why these are in C */
exports.setMap = map => native.setMap(map.trim());
exports.addSprite = native.addSprite;
exports.getGrid = native.getGrid;
exports.getTile = native.getTile;
exports.tilesWith = native.tilesWith;
exports.clearTile = native.clearTile;
exports.getFirst = native.getFirst;
exports.getAll = native.getAll;
exports.width = native.width;
exports.height = native.height;
exports.setBackground = native.setBackground;

/* opts: x, y, color (all optional) */
exports.addText = (str, opts={}) => {
  console.log("engine.js:addText");
  const CHARS_MAX_X = 21;
  const padLeft = Math.floor((CHARS_MAX_X - str.length)/2);

  native.text_add(
    str,
    opts.color ?? [10, 10, 40],
    opts.x ?? padLeft,
    opts.y ?? 0
  );
}

exports.clearText = () => native.text_clear();


exports.setLegend = (...bitmaps) => {
  console.log("engine.js:setLegend");
  native.legend_clear();
  for (const [charStr, bitmap] of bitmaps) {
    native.legend_doodle_set(charStr, bitmap.trim());
  }
  native.legend_prepare();
};

exports.setSolids = solids => {
  console.log("engine.js:setSolids");
  native.solids_clear();
  solids.forEach(native.solids_push);
};

exports.setPushables = pushTable => {
  console.log("engine.js:setPushables");
  native.push_table_clear();
  for (const [pusher, pushesList] of Object.entries(pushTable))
    for (const pushes of pushesList)
      native.push_table_set(pusher, pushes);
};

let afterInputs = [];
exports.afterInput = fn => (console.log('afterInputs'), afterInputs.push(fn));

const button = {
  pinToHandlers: {
     "5": [],
     "7": [],
     "6": [],
     "8": [],
    "12": [],
    "14": [],
    "13": [],
    "15": [],
  },
  keyToPin: {
    "w":  "5",
    "s":  "7",
    "a":  "6",
    "d":  "8",
    "i": "12",
    "k": "14",
    "j": "13",
    "l": "15",
  }
};

const press = pin => {
  button.pinToHandlers[pin].forEach(f => f());

  afterInputs.forEach(f => f());

  native.map_clear_deltas();
};

exports.onInput = (key, fn) => {
  console.log("engine.js:onInput");
  const pin = button.keyToPin[key];

  if (pin === undefined)
    throw new Error(`the sprig doesn't have a "${key}" button!`);

  button.pinToHandlers[pin].push(fn);
};

function _makeTag(cb) {
  return (strings, ...interps) => {
    if (typeof strings === "string") {
      throw new Error("Tagged template literal must be used like name`text`, instead of name(`text`)");
    }
    const string = strings.reduce((p, c, i) => p + c + (interps[i] ?? ''), '');
    return cb(string);
  }
}
exports.bitmap = _makeTag(text => text);
exports.tune = _makeTag(text => text);
exports.map = _makeTag(text => text);

const tunePoll = (() => {
  const instrumentKey = {
    '~': 'sine',
    '-': 'square',
    '^': 'triangle',
    '/': 'sawtooth',
  };
  const INSTRUMENTS = ["sine", "square", "triangle", "sawtooth"];

  function *playTuneHelper(nrs, number) {
    console.log("in play tune helper!");

    for (let i = 0; i < tune.length*number; i++) {
      while (native.note_reader_step(nrs))
        yield true;
      native.note_reader_reset(nrs);
    }
    
    native.note_reader_free(nrs);
  }

  let tunes = [];

  let ret = () => {
    tunes = tunes.filter(t => t.generator.next().value);
  };

  exports.playTune = function(tune, number = 1) {
    console.log("engine.js:playTune");
    /* this code originally used async with promises
     * we're switching to generators that poll
     *
     * (because we don't trust kaluma's async runtime,
     *  and because having control over when the poll
     *  happens relative to the other things our "operating
     *  system" does on a regular basis is useful)
     */

    const nrs = native.note_reader_alloc(tune);
    const ret = {
      generator: playTuneHelper(nrs, number),
      end() {
        native.note_reader_free(nrs);
        tunes = tunes.filter(x => x != this);
      },
      isPlaying() { return tunes.some(x => x == this); },
    }

    tunes.push(ret);
    return ret;
  }
  
  return ret;
})();

setInterval(() => {
  /* I have no idea how this is going to hold up if you reload the module
     I'm not clear on when init gets called. */

  const width = 128, height = 160;
  const pixels = new Uint16Array(width * height);
  const { render } = native;
  
  pixels.fill(gc.color16(0, 0, 0));
  tunePoll();

  /* gobble up all yummy tasty input events */
  while (true) {
    const pin = audio.press_queue_try_remove();
    if (pin === undefined) break;
    press(pin);
  }

  render(pixels, 0);
  screen.fillImage(0, 0, width, height, new Uint8Array(pixels.buffer));
}, 1000/20);
