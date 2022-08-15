const {
  /* sprite interactions */ setSolids, setPushables,
  /*              see also: sprite.x +=, sprite.y += */

  /* art */ setLegend, setBackground,
  /* text */ addText, clearText,

  /*   spawn sprites */ setMap, addSprite,
  /* despawn sprites */ clearTile, /* sprite.remove() */

  /* tile queries */ getGrid, getTile, getFirst, getAll, tilesWith,
  /* see also: sprite.type */

  /* map dimensions */ width, height,

  /* constructors */ bitmap, tune, map,

  /* input handling */ onInput, afterInput,

  /* how much sprite has moved since last onInput: sprite.dx, sprite.dy */
} = require("engine");
/*
@title: pyre
@author: ced

Instructions:

Burn down the blue hut.
*/

const smallFire = ['0', '1'];
const   bigFire = ['2', '3'];
const   allFire = [...smallFire, ...bigFire];
const       log = 'l';
const    player = 'p';
const      cube = 'c';
const     house = 'h';
const     grass = 'g';
setLegend(
  [ smallFire[0], bitmap`
................
................
.....3.3........
....3.....3.....
...3.3.3.3.3....
..3.3.3.3.3.....
...3.333.3...3..
..3.3.3.3...3...
.3.333.3.....3..
..3.333.3.3.3...
.3...3.3.3.3.3..
......3.333.3...
...3.333.3.3....
....3.333.......
.......3........
................
`],
  [ smallFire[1], bitmap`
................
................
........3.3.....
.....3.....3....
....3.3.3.3.3...
.....3.3.3.3.3..
..3...3.333.3...
...3...3.3.3.3..
..3.....3.333.3.
...3.3.3.333.3..
..3.3.3.3.3...3.
...3.333.3......
....3.3.333.3...
.......333.3....
........3.......
................`],
  [ bigFire[0], bitmap`
................
......3.3.......
.3.3.3.3........
..3.3.....3.3...
.3.3.3.3.3.3....
..3.3.3.3.3.....
.3.33333.3...3..
..3.333.3...3.3.
.3.333.3.....3..
3.3.333.3.3.3.3.
.3...333.333.3..
....3.33333.3.3.
...3.33333.3.3..
....3.333.3.3...
.......3........
................`],
  [ bigFire[1], bitmap`
................
.......3.3......
........3.3.3.3.
...3.3.....3.3..
....3.3.3.3.3.3.
.....3.3.3.3.3..
..3...3.33333.3.
.3.3...3.333.3..
..3.....3.333.3.
.3.3.3.3.333.3.3
..3.333.333...3.
.3.3.33333.3....
..3.3.33333.3...
...3.3.333.3....
........3.......
................`],
  [ log, bitmap`
................
................
..........44....
........44..4...
......44.....4..
.....4.......4..
....4........4..
...4.........4..
..4444......4...
.44..44....4....
.4....4...4.....
.4....4..4......
.4....4.4.......
..4...44........
...444..........
................`],
  [ player, bitmap`
................
................
................
......444.......
.....4...4......
.....40.04......
....44...44.....
...4.40004.4....
...4.4...4.4....
......444.......
.....4...4......
.....4...4......
....44...44.....
................
................
................`],
  [ cube, bitmap`
......4444......
....44....44....
..44........44..
.4............4.
4.44........44.4
4...44....44...4
4.4...4444...4.4
4...4...4....4.4
4.4.4.4.4.4..4.4
4.4.4...4......4
4.....4.4.4..4.4
.4..4.4.4.4...4.
..44....4...44..
....44..4.44....
......4444......
................`], 
  [ house, bitmap`
................
................
................
................
........5.......
......55.55.....
....55.....55...
...5.........5..
..5...........5.
....555555555...
.....5.....5....
....5.......5...
...5.........5..
...5.........5..
..5....555....5.
...555.555.555..`],
  [ grass, bitmap`
..4.........4...
...4.........4..
...44........4..
....4.........4.
....44........4.
.....4........4.
.....44......4..
.....44...4.....
.....444...4....
4.....44....4...
.4....44....4...
.4...444.....4..
.4...44......4..
.4...........4..
4...........4...
................`]
)

setSolids([player, log, cube]);
setPushables({ [player]: [cube] })

const      isGrass = tile => tile.type == grass;
const     burnsBig = tile => tile.type == log || tile.type == cube;
const needsBigFire = tile => tile.type == house;

const isSmallFire = tile => smallFire.includes(tile.type);
const      isFire = tile => allFire.includes(tile.type);

const fireTiles = () => allFire.flatMap(getAll);
const neighborTiles = tile => {
  return [
    getTile(tile.x+1, tile.y+0),                                 
    getTile(tile.x-1, tile.y+0),                                 
    getTile(tile.x+0, tile.y+1),                                 
    getTile(tile.x+0, tile.y-1),                                 
    getTile(tile.x,   tile.y)
  ]
  .flat();
}

const replace = (type0, type1) => {
  for (const sprite of getAll(type0)) {
    sprite.type = type1;
  }
}

let tick = 0;
setInterval(() => {
  tick++;

  /* fire flicker */
  if (tick % 2) {
    replace(smallFire[0], smallFire[1]);
    replace(  bigFire[0],   bigFire[1]);
  } else {
    replace(smallFire[1], smallFire[0]);
    replace(  bigFire[1],   bigFire[0]);
  }

  /* fire spread */
  if (tick % 4 == 0) {        
    // changing the map while iterating over can create bugs,
    // so we'll store the tiles we want to replace with fire here
    const replacements = new Map();
    
    for (const fire of fireTiles()) {            
      for (const tile of neighborTiles(fire)) {
        if (isFire(tile)) continue;
        if (isSmallFire(fire) && needsBigFire(tile)) continue;
        
        replacements.set(
          tile,
          burnsBig(tile) ? bigFire[0] : smallFire[0]
        );
      }
      
      fire.remove();
    }

    // apply all of the replacements we stored
    for (const [tile, type] of replacements) {
      tile.type = type;
    }
  }

  /* win condition */
  if (getAll(house).length == 0 && fireTiles().length == 0) {
    levels[1+level] && setMap(levels[++level]);
    addText("smol been", { x: 6, y: 0, color: [ 200, 20, 20 ]});
    addText("big rage", { x: 6, y: 14, color: [ 200, 20, 20 ]});
  }
}, 200);

afterInput(() => {
  /* crate kill grass */
  for (const { x, y } of getAll('c')) {
    for (const g of getTile(x, y).filter(isGrass)) {
      g.remove();
    }
  }
})

let level = 0;
const levels = [
    map`
.........
...g.g.p.
..gg.gg..
.gg...g..
..1...gl.
......ll.
..lllll..
..hl.....
.........`,
    map`
.........
..gg.....
.gggg....
.ghgg....
.ggg..c..
gggg.....
gg...p...
g1.......
.........`,
    map`
..llllggg
........g
........g
..h.....g
......c.g
..p.....g
........g
.......gg
.1gggggg.`,
]
// .reverse();

const pushPlayer = (dx, dy) => {
  if (getFirst(player)) {
    getFirst(player).x += dx;
    getFirst(player).y += dy;
  }
}
onInput("a", () => {
  pushPlayer(-1,  0);
})
onInput("d", () => {
  pushPlayer( 1,  0);
})
onInput("w", () => {
  pushPlayer( 0, -1);
})
onInput("s", () => {
  pushPlayer( 0,  1);
})

setMap(levels[level]);

onInput(    "j", () => {
  setMap(levels[level]);
});
