#!/usr/bin/env node
// Render the 4 DeskBuddy scene-mode design concepts (working + idle) at 80x48
// logical pixels -> PNG contact sheet via PPM + PIL.
const fs = require('fs');

const W = 80, H = 48;
const PX = 4;      // pixel size for the device-size row
const CS = 320;    // 80*4 widget row
const GAP = 4, CAP = 20;

const C = {
  skyD: '#8ED6F0', skyN: '#101A38', hillD: '#54B264', hillD2: '#3F9E52',
  dirt: '#C98A4E', dirt2: '#B3763F', grass: '#3FA04C',
  groundN: '#274A5C', groundN2: '#1E3A49',
  water: '#1B2F55', waterHi: '#2E4E85',
  brick: '#C4622D', brickLine: '#7E3D18', brickHi: '#D97A3C',
  q: '#F3C22C', qDk: '#8A6206',
  pipe: '#2E9E57', pipeHi: '#46C06F', pipeDk: '#1C6E3B',
  coin: '#FFD84A', coinDk: '#B8860B',
  hat: '#E13030', hatDk: '#9E1F1F',
  skin: '#FFC9A3', skinDk: '#E8A878',
  shirt: '#1F5FD6', shirtDk: '#17438F',
  over: '#2455B8', overDk: '#173A82',
  boot: '#5A3A1C',
  eye: '#222222',
  moon: '#F3ECD3', moonDk: '#D9CFAE',
  star: '#EDE6C9',
  cloud: '#FFFFFF', cloudN: '#2A3A5C',
  tree: '#1E7A46', treeDk: '#155C34', trunk: '#7A4E2A',
  wind: '#E8E2D2', windDk: '#B8AF9A',
  blade: '#D8D2C0',
  snow: '#F4F9FF',
  glow: '#3A4F86',
};
const p = (c) => {
  const h = c.replace('#', '');
  return [parseInt(h.slice(0, 2), 16), parseInt(h.slice(2, 4), 16), parseInt(h.slice(4, 6), 16)];
};

function Canvas() { return { w: W, h: H, d: new Uint8Array(W * H * 3) }; }
function put(c, x, y, col) {
  const [r, g, b] = p(col);
  x = Math.round(x); y = Math.round(y);
  if (x < 0 || y < 0 || x >= c.w || y >= c.h) return;
  const i = (y * c.w + x) * 3;
  c.d[i] = r; c.d[i + 1] = g; c.d[i + 2] = b;
}
function rect(c, x, y, w, h, col) {
  for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) put(c, x + i, y + j, col);
}
function circle(c, cx, cy, r, col) {
  for (let j = -r - 1; j <= r + 1; j++) for (let i = -r - 1; i <= r + 1; i++)
    if (i * i + j * j <= r * r + 0.5) put(c, cx + i, cy + j, col);
}
function ppm(c, path) {
  let out = 'P6\n' + c.w + ' ' + c.h + '\n255\n';
  const buf = Buffer.from(c.d);
  fs.writeFileSync(path, Buffer.concat([Buffer.from(out), buf]));
}

// ---------- shared sprites ----------
const FRAME = 8;
function char(c, x, y, f, blink) {
  f = f % FRAME;
  const legA = [[0, -1], [0, -2], [1, -1], [0, 0], [-1, 0], [0, 1], [1, 2], [0, 1]][f];
  const legB = [[0, 1], [0, 2], [-1, 1], [0, 0], [1, 0], [0, -1], [-1, -2], [0, -1]][f];
  const armA = [1, 1, 0, 0, -1, -1, 0, 0][f];
  // boots
  rect(c, x + 2 + legA[0], y + 15, 3, 1, C.boot);
  rect(c, x + 5 + legB[0], y + 15, 3, 1, C.boot);
  // legs (overalls)
  rect(c, x + 3 + legA[0], y + 14, 2, 1, C.over);
  rect(c, x + 5 + legB[0], y + 13, 2, 2, C.over);
  // torso
  rect(c, x + 2, y + 11, 6, 3, C.shirt);
  rect(c, x + 3, y + 10, 4, 2, C.over);
  put(c, x + 3, y + 10, C.q); put(c, x + 6, y + 10, C.q);
  // arms
  if (armA >= 0) rect(c, x + 1, y + 11, 1, 2 + armA, C.shirtDk);
  else rect(c, x + 8, y + 11, 1, 2 + (-armA), C.shirtDk);
  // head
  rect(c, x + 2, y + 7, 6, 3, C.skin);
  put(c, x + 7, y + 9, C.skinDk);
  rect(c, x + 4, y + 8, 1, blink ? 1 : 2, blink ? C.skin : C.eye);
  // hat
  rect(c, x + 1, y + 5, 8, 2, C.hat);
  rect(c, x + 0, y + 7, 4, 1, C.hatDk);
  put(c, x + 4, y + 6, '#FFFFFF');
}
function idleChar(c, x, y, f, blink) {
  // standing, arm resting
  rect(c, x + 2, y + 15, 3, 1, C.boot);
  rect(c, x + 5, y + 15, 3, 1, C.boot);
  rect(c, x + 3, y + 14, 2, 1, C.over);
  rect(c, x + 5, y + 14, 2, 1, C.over);
  rect(c, x + 2, y + 11, 6, 3, C.shirt);
  rect(c, x + 3, y + 10, 4, 2, C.over);
  rect(c, x + 1, y + 11, 1, 3, C.shirtDk);
  rect(c, x + 8, y + 11, 1, 3, C.shirtDk);
  rect(c, x + 2, y + 7, 6, 3, C.skin);
  rect(c, x + 4, y + 8, 1, blink ? 1 : 2, blink ? C.skin : C.eye);
  rect(c, x + 1, y + 5, 8, 2, C.hat);
  rect(c, x + 0, y + 7, 4, 1, C.hatDk);
}
function pipe(c, x, y, h) {
  rect(c, x, y - h, 9, h, C.pipe);
  rect(c, x, y - h, 2, h, C.pipeHi);
  rect(c, x + 7, y - h, 2, h, C.pipeDk);
  rect(c, x - 1, y - h, 11, 3, C.pipe);
  rect(c, x - 1, y - h, 2, 3, C.pipeHi);
  rect(c, x + 8, y - h, 3, 3, C.pipeDk);
}
function coin(c, x, y, f) {
  const ph = Math.round(((f * 2 + x) % FRAME) / 2);
  const col = ph % 2 ? C.coin : C.coinDk;
  put(c, x, y - 1, col); put(c, x, y, col); put(c, x, y + 1, col);
}
function qblock(c, x, y, f) {
  rect(c, x, y, 7, 7, C.q);
  for (let i = 0; i < 7; i++) { put(c, x, y + i, C.qDk); put(c, x + 6, y + i, C.qDk); put(c, x + i, y, C.qDk); put(c, x + i, y + 6, C.qDk); }
  rect(c, x + 1, y + 1, 5, 5, C.q);
  const bright = Math.round(((f * 2) % FRAME) / 2) % 2 === 0;
  const qcol = bright ? '#FFFFFF' : C.qDk;
  put(c, x + 2, y + 2, qcol); put(c, x + 4, y + 2, qcol);
  put(c, x + 3, y + 3, qcol); put(c, x + 4, y + 3, qcol);
  put(c, x + 3, y + 4, qcol);
  put(c, x + 4, y + 5, qcol);
}
function groundD(c) {
  rect(c, 0, 43, W, 5, C.dirt);
  for (let x = 0; x < W; x++) put(c, x, 43, C.grass);
  for (let x = 0; x < W; x += 3) rect(c, x + 1, 44, 1, 2, C.dirt2);
}
function groundN(c) {
  rect(c, 0, 43, W, 5, C.groundN);
  for (let x = 0; x < W; x++) put(c, x, 43, C.groundN2);
}
function cloud(c, x, y, col, f) {
  rect(c, x, y, 8, 2, col);
  rect(c, x + 1, y - 1, 5, 1, col);
  rect(c, x + 2, y + 2, 5, 1, col);
}
function star(c, x, y, f, bright) {
  if (Math.round(((f + x) % FRAME) / 2) % 2 === 0 && bright) return;
  put(c, x, y, C.star);
}
function bird(c, x, y, f) {
  const up = Math.round(f / 2) % 2 === 0;
  if (up) { put(c, x, y, C.eye); put(c, x + 1, y - 1, C.eye); put(c, x + 2, y, C.eye); }
  else { put(c, x, y - 1, C.eye); put(c, x + 1, y, C.eye); put(c, x + 2, y - 1, C.eye); }
}
function zzz(c, x, y, f) {
  // Clear Z: top bar, diagonal, bottom bar. Animated: shift right + fade.
  const shift = Math.round(f / 2) % 2;
  const col = shift ? '#E8F0F8' : '#9FB8C8';
  rect(c, x, y, 3, 1, col);
  put(c, x + 1, y + 1, col);
  rect(c, x, y + 2, 3, 1, col);
  // small second Z up-right
  put(c, x + 5, y - 2, col);
  put(c, x + 6, y - 2, col);
  put(c, x + 6, y - 1, col);
  put(c, x + 5, y - 1, col);
}

// ---------- concept 1: PLUMBER'S RUN ----------
function c1(f, night) {
  const c = Canvas();
  if (!night) {
    rect(c, 0, 0, W, H, C.skyD);
    circle(c, 10, 8, 4, '#FFE066');
    cloud(c, 50 + (Math.round(f / 4) % 2), 10, C.cloud, f);
    cloud(c, 18 + (Math.round((f + 4) / 4) % 2), 18, C.cloud, f);
    groundD(c);
    pipe(c, 8, 43, 12);
    qblock(c, 34, 22, f);
    qblock(c, 60, 22, f);
    for (let i = 0; i < 5; i++) coin(c, 44 + i * 2, 36, f + i);
    char(c, 56, 24, f, false);
  } else {
    rect(c, 0, 0, W, H, C.skyN);
    circle(c, 62, 9, 5, C.moon);
    put(c, 60, 7, C.moonDk); put(c, 64, 10, C.moonDk);
    for (let i = 0; i < 12; i++) star(c, (i * 7 + 3) % 78, (i * 5 + 2) % 20 + 1, f, i % 3 === 0);
    groundN(c);
    pipe(c, 8, 43, 12);
    idleChar(c, 34, 24, f, Math.round(f / 2) % 2 === 1);
    zzz(c, 42, 18, f);
  }
  return c;
}

// ---------- concept 2: VALLEY / WINDMILL ----------
function hills(c, col) {
  for (let x = 0; x < W; x++) {
    const h = 26 + Math.round(5 * Math.sin(x / 9.0) + 3 * Math.sin(x / 4.1));
    rect(c, x, h, 1, 43 - h, col);
  }
}
function c2(f, night) {
  const c = Canvas();
  if (!night) {
    rect(c, 0, 0, W, H, C.skyD);
    circle(c, 66, 9, 4, '#FFE066');
    cloud(c, 12 + (Math.round(f / 4) % 3), 12, C.cloud, f);
    cloud(c, 44 + (Math.round((f + 6) / 4) % 3), 7, C.cloud, f);
    bird(c, 20 + Math.round(f / 2) % 3, 15, f);
    bird(c, 28 + Math.round(f / 2) % 3, 12, f);
    hills(c, C.hillD);
    rect(c, 0, 43, W, 5, C.grass);
    // windmill on the right
    rect(c, 58, 20, 4, 23, C.wind);
    rect(c, 56, 18, 8, 3, C.windDk);
    const a = (f / FRAME) * Math.PI * 2;
    for (let k = 0; k < 4; k++) {
      const ang = a + k * Math.PI / 2;
      const dx = Math.round(Math.cos(ang) * 7), dy = Math.round(Math.sin(ang) * 7);
      put(c, 60 + dx, 16 + dy, C.blade);
      if (Math.abs(dx) > 2) put(c, 60 + dx, 16 + dy + (dy > 0 ? 1 : 0), C.blade);
      if (Math.abs(dy) > 2) put(c, 60 + dx, 16 + dy + (dx > 0 ? 1 : 0), C.blade);
    }
    put(c, 60, 16, C.windDk);
    // tree left
    rect(c, 14, 38, 2, 5, C.trunk);
    circle(c, 15, 34, 4, C.tree);
    circle(c, 13, 36, 3, C.treeDk);
  } else {
    rect(c, 0, 0, W, H, C.skyN);
    circle(c, 14, 9, 5, C.moon);
    put(c, 12, 8, C.moonDk); put(c, 16, 11, C.moonDk);
    for (let i = 0; i < 12; i++) star(c, (i * 11 + 5) % 78, (i * 6 + 3) % 18 + 1, f, i % 3 === 0);
    hills(c, C.groundN2);
    rect(c, 0, 43, W, 5, C.groundN);
    // windmill silhouette (tower + blades) — visible against the dark sky
    rect(c, 58, 20, 4, 23, '#16323F');
    rect(c, 56, 18, 8, 3, '#10262F');
    const an = (f / FRAME) * Math.PI * 2;
    for (let k = 0; k < 4; k++) {
      const ang = an + k * Math.PI / 2;
      const dx = Math.round(Math.cos(ang) * 7), dy = Math.round(Math.sin(ang) * 7);
      put(c, 60 + dx, 16 + dy, '#4A7A94');
      put(c, 60 + dx / 2, 16 + dy / 2, '#4A7A94');
    }
    put(c, 60, 16, '#10262F');
    // sleeping cat at the windmill base
    rect(c, 55, 40, 6, 2, '#3A3A44');
    circle(c, 54, 40, 1, '#3A3A44');
    put(c, 54, 39, C.star);
    // tree silhouette
    rect(c, 14, 38, 2, 5, '#102630');
    circle(c, 15, 34, 4, '#16394A');
  }
  return c;
}

// ---------- concept 3: THE ROBOT ----------
function desk(c) {
  rect(c, 12, 36, 56, 2, '#5A4632');   // desktop
  rect(c, 15, 38, 3, 5, '#4A3A28');
  rect(c, 62, 38, 3, 5, '#4A3A28');
}
function robot(c, f, night) {
  const x = 36, y = 14;
  // body
  rect(c, x, y + 6, 10, 8, '#8A93A8');
  rect(c, x + 1, y + 7, 8, 2, night ? '#243A5A' : '#1F5FD6'); // chest panel
  // head
  rect(c, x + 1, y, 8, 6, '#9AA3B8');
  rect(c, x + 2, y + 1, 6, 3, night ? '#243A5A' : '#101820'); // face
  if (!night) { // eyes + scanline
    put(c, x + 3, y + 2, '#4EC9B0'); put(c, x + 6, y + 2, '#4EC9B0');
    const scan = (f % 3) * 1;
    rect(c, x + 3, y + 1 + scan, 4, 1, '#4EC9B0');
  } else { // sleeping: eye lines
    put(c, x + 3, y + 2, '#4EC9B0'); put(c, x + 6, y + 2, '#4EC9B0');
  }
  put(c, x + 4, y - 1, '#5A6378'); // antenna
  put(c, x + 4, y - 2, f % 2 ? '#E05656' : '#FF8A8A');
  // arms -> keyboard
  const reach = 6;
  rect(c, x - 2, y + 8, 2, 2, '#6E7890');
  rect(c, x + 10, y + 8, 2, 2, '#6E7890');
  const hx = 58;
  rect(c, x, y + 12, reach, 1, '#6E7890');
  rect(c, x + 10, y + 12, hx + 2 - (x + 10), 1, '#6E7890');
  // keyboard
  rect(c, 54, 35, 10, 1, '#3A4356');
  if (!night && Math.round(f / 2) % 2 === 0) put(c, 56 + (f % 3), 34, '#4EC9B0');
}
function c3(f, night) {
  const c = Canvas();
  if (!night) {
    rect(c, 0, 0, W, H, '#2A2A36');
    // monitor
    rect(c, 50, 16, 18, 12, '#1A1A22');
    rect(c, 52, 18, 14, 8, '#0E3B2E');
    for (let i = 0; i < 4; i++) {
      const len = (f * 3 + i * 5) % 9;
      rect(c, 53, 19 + i * 2, Math.min(len, 12), 1, '#4EC9B0');
    }
    rect(c, 57, 28, 4, 2, '#1A1A22');
    desk(c);
    robot(c, f, night);
    // floor
    rect(c, 0, 43, W, 5, '#20202B');
  } else {
    rect(c, 0, 0, W, H, '#17171F');
    // window with moon — clear frame so it reads as a window
    rect(c, 10, 10, 14, 10, '#3E5A7E');        // frame
    rect(c, 12, 12, 10, 6, '#243A5A');          // night sky inside
    circle(c, 16, 15, 2, C.moon);
    put(c, 19, 13, C.star); put(c, 14, 17, C.star);
    desk(c);
    // screen off
    rect(c, 50, 16, 18, 12, '#101018');
    rect(c, 57, 28, 4, 2, '#101018');
    robot(c, f, true);
    // cat asleep on desk
    rect(c, 20, 34, 6, 2, '#3A3A44');
    circle(c, 19, 34, 1, '#3A3A44');
    zzz(c, 24, 30, f);
    rect(c, 0, 43, W, 5, '#121218');
  }
  return c;
}

// ---------- concept 4: THE VOYAGE ----------
function sail(c, f, night) {
  const bob = Math.round(Math.sin(f / FRAME * Math.PI * 2));
  const bx = 40, by = 34 + bob;
  // sail
  for (let j = 0; j < 10; j++) rect(c, bx + 5 + j, by - 10 + j, 6 - j, 1, night ? '#C9D2E8' : '#F4F4F0');
  // hull
  rect(c, bx, by, 20, 3, night ? '#4A3A28' : '#7A4E2A');
  rect(c, bx + 2, by + 3, 16, 2, night ? '#3A2E20' : '#5A3A1C');
  put(c, bx + 5, by - 10, C.windDk);
}
function c4(f, night) {
  const c = Canvas();
  if (!night) {
    rect(c, 0, 0, W, H, C.skyD);
    circle(c, 66, 8, 4, '#FFE066');
    // choppy waves
    for (let x = 0; x < W; x++) {
      const h = 30 + Math.round(3 * Math.sin((x + f * 2) / 4.5));
      rect(c, x, h, 1, 43 - h, '#2E86C0');
      if (Math.round((x + f * 2) / 4.5) % 7 < 1) put(c, x, h, '#DFF3FF');
    }
    rect(c, 0, 43, W, 5, '#1F6A9E');
    bird(c, 12, 14, f);
    sail(c, f, false);
  } else {
    rect(c, 0, 0, W, H, C.skyN);
    circle(c, 18, 9, 5, C.moon);
    put(c, 16, 8, C.moonDk); put(c, 20, 11, C.moonDk);
    for (let i = 0; i < 10; i++) star(c, (i * 13 + 8) % 78, (i * 7 + 2) % 18 + 1, f, i % 3 === 0);
    // calm water + moon reflection
    for (let x = 0; x < W; x++) {
      const h = 32 + (x % 3 === 0 ? 1 : 0);
      rect(c, x, h, 1, 43 - h, C.water);
    }
    const shimmer = Math.round(f / 2) % 2 === 0;
    for (let j = 0; j < 8; j++) {
      const w = 2 + (j % 3) + (shimmer ? 1 : 0);
      rect(c, 18 - Math.floor(w / 2), 33 + j, w, 1, C.moon);
    }
    // moored boat
    const bx = 48, by = 35;
    rect(c, bx, by, 16, 3, '#3A2E20');
    rect(c, bx + 2, by + 3, 12, 2, '#2A2118');
    put(c, bx + 8, by - 6, C.windDk);
    rect(c, bx + 8, by - 6, 1, 6, C.windDk);
    rect(c, 0, 43, W, 5, '#12203C');
  }
  return c;
}

// ---------- sheet ----------
const sheet = { w: 4 * (CS + GAP) + GAP, h: 3 * (CS + CAP) + 4 * GAP + CAP, d: new Uint8Array(0) };
sheet.d = new Uint8Array(sheet.w * sheet.h * 3);
const paint = (c, ox, oy) => { for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  const si = (y * W + x) * 3, di = ((oy + y * PX) * sheet.w + ox + x * PX) * 3;
  for (let i = 0; i < PX * PX; i++) {
    const r = Math.floor(i / PX), cc = i % PX;
    const di2 = (di + (r * sheet.w + cc) * 3);
    sheet.d[di2] = c.d[si]; sheet.d[di2 + 1] = c.d[si + 1]; sheet.d[di2 + 2] = c.d[si + 2];
  }
}};

const F = 3; // representative frame (mid-cycle)
const concepts = [
  ['1 PLUMBER RUN — day/working', () => c1(F, false), () => c1(F, true)],
  ['2 VALLEY WINDMILL — day/working', () => c2(F, false), () => c2(F, true)],
  ['3 ROBOT AT DESK — day/working', () => c3(F, false), () => c3(F, true)],
  ['4 THE VOYAGE — day/working', () => c4(F, false), () => c4(F, true)],
];
const nightLabels = ['NIGHT/IDLE', 'NIGHT/IDLE', 'NIGHT/IDLE', 'NIGHT/IDLE'];

// row 0: day; row 1: night; row 2: device size (night, 128x128)
for (let col = 0; col < 4; col++) {
  const [label, day, night] = concepts[col];
  const ox = GAP + col * (CS + GAP);
  const cy0 = GAP, cy1 = GAP + CS + CAP + GAP;
  paint(day(), ox, cy0);
  paint(night(), ox, cy1);
  const dev = night();
  paint(dev, ox + Math.floor((CS - W * 4) / 2), cy1 + CS + CAP + GAP + Math.floor((CS - W * 4) / 2));
  ppm(sheet, 'C:/Users/m/AppData/Local/hermes/cache/scratch/sheet_raw.ppm');
}
ppm(sheet, 'C:/Users/m/AppData/Local/hermes/cache/scratch/sheet_raw.ppm');
console.log('sheet', sheet.w, sheet.h);
console.log('labels:', JSON.stringify(concepts.map(c => c[0])));
