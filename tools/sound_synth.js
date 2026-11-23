// The app's sound effects, made from code: every take of every sound (tools/app_sounds.js renders
// the ones chosen in app/sounds.json). The same code runs in the Sound Lab page used to choose and
// tweak them, so keep the two in step: the page's script between SYNTH-BEGIN and SYNTH-END is this
// file without these lines and the export at the end.
const Synth = (() => {
  const NOTE = { C: 0, 'C#': 1, D: 2, 'D#': 3, E: 4, F: 5, 'F#': 6, G: 7, 'G#': 8, A: 9, 'A#': 10, B: 11 };
  const hz = (n) => {
    if (typeof n === 'number') return n;
    const m = /^([A-G]#?)(\d)$/.exec(n);
    if (!m) throw new Error('Unknown note ' + n);
    return 440 * Math.pow(2, ((Number(m[2]) + 1) * 12 + NOTE[m[1]] - 69) / 12);
  };
  // Deterministic noise, so a sound renders the same every time (and on the PC when it's exported).
  const rng = (seed) => {
    let s = (seed >>> 0) || 1;
    return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 2147483648 - 1; };
  };
  const hash = (str) => {
    let h = 2166136261;
    for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 16777619); }
    return h >>> 0;
  };

  // Voices: fm = bell/glass tone (sine modulated by a sine), pluck = Karplus-Strong string,
  // pulse = square wave (band-limited), noise = filtered noise burst, whoosh = noise through a
  // resonant band-pass that sweeps from one pitch to another. Times in seconds. A slideTo glides
  // over glide seconds (else the whole sound); cents detunes. A { kind: 'space' } entry adds a
  // small room (mix, size) to the whole sound.
  const DEFAULTS = {
    fm: { gain: 1, ratio: 2, index: 2, idecay: 0.05, attack: 0.002, decay: 0.1, release: 0.01 },
    pluck: { gain: 1, bright: 0.6, damp: 0.996, attack: 0.0005, decay: 0, release: 0.012 },
    pulse: { gain: 1, duty: 0.5, attack: 0.002, decay: 0, release: 0.008 },
    noise: { gain: 1, lp: 8000, hp: 1000, attack: 0.0005, decay: 0.01, release: 0.004 },
    whoosh: { gain: 1, from: 800, to: 4000, q: 0.8, attack: 0.04, decay: 0.08, release: 0.02 },
  };
  const F = (note, t, dur, o) => Object.assign({ kind: 'fm', note, t, dur }, o);
  const S = (note, t, dur, o) => Object.assign({ kind: 'pluck', note, t, dur }, o);
  const Q = (note, t, dur, o) => Object.assign({ kind: 'pulse', note, t, dur }, o);
  const N = (t, dur, o) => Object.assign({ kind: 'noise', t, dur }, o);
  const W = (t, dur, o) => Object.assign({ kind: 'whoosh', t, dur }, o);
  const R = (mix, size) => ({ kind: 'space', mix, size });
  const strum = (notes, t, gap, dur, o) => notes.map((n, i) => S(n, t + i * gap, dur - i * gap, o));
  // A soft pop: a pure tone that bends from one pitch to another.
  const P = (from, to, t, dur, o) => F(from, t, dur, Object.assign({ slideTo: to, glide: 0.02, ratio: 1, index: 0, attack: 0.002 }, o));
  // A mallet on a wooden bar: a bright knock that settles into a round tone.
  const M = (note, t, dur, o) => F(note, t, dur, Object.assign({ ratio: 4, index: 1.2, idecay: 0.007, attack: 0.001, decay: 0.07 }, o));
  // Two tones a little apart in pitch, for a soft shimmer.
  const pad = (note, t, dur, o) => [F(note, t, dur, Object.assign({ cents: -7 }, o)), F(note, t, dur, Object.assign({ cents: 7 }, o))];

  const FAMILIES = [
    { id: 'air', name: 'Air', about: 'Soft pops that bend in pitch, airy whooshes and a little room, in the spirit of a modern console\'s TV-mode menus.' },
    { id: 'felt', name: 'Felt', about: 'Muted wooden taps and rounded chimes with a little room. Warmer, with the same modern feel.' },
    { id: 'glass', name: 'Glass', about: 'Soft bell tones. Clean and quiet, like a modern system menu.' },
    { id: 'string', name: 'String', about: 'Plucked strings from a guitar-string model. Warmer, and close to the app\'s guitar banner jingle.' },
    { id: 'chip', name: 'Chip', about: 'Square waves like an old handheld. Fits the DS theme.' },
  ];

  const EFFECTS = [
    { id: 'move', group: 'Moving around', name: 'Move', when: 'Choosing a game with Left/Right.', loud: 0.4, sets: {
      air: [P('C#6', 'E6', 0, 0.09, { glide: 0.012, decay: 0.028 }), N(0, 0.008, { lp: 12000, hp: 4000, decay: 0.0015, gain: 0.12 }), R(0.12, 0.8)],
      felt: [M('G5', 0, 0.14, { decay: 0.045 }), R(0.1, 0.6)],
      glass: [F('B6', 0, 0.14, { ratio: 3.5, index: 2.2, idecay: 0.02, decay: 0.035 })],
      string: [S('E6', 0, 0.2, { bright: 0.55, damp: 0.993 })],
      chip: [Q('E6', 0, 0.05, { duty: 0.25, decay: 0.025 })],
    } },
    { id: 'tick', group: 'Moving around', name: 'Scroll', when: 'Moving through a list with Up/Down.', loud: 0.28, sets: {
      air: [P('F#6', 'G#6', 0, 0.05, { glide: 0.008, decay: 0.014 }), R(0.08, 0.7)],
      felt: [M('D6', 0, 0.08, { decay: 0.022, index: 0.9 }), R(0.06, 0.6)],
      glass: [F('E7', 0, 0.08, { ratio: 3.5, index: 1.4, idecay: 0.012, decay: 0.018 })],
      string: [S('B6', 0, 0.1, { bright: 0.4, damp: 0.985 })],
      chip: [Q('B6', 0, 0.025, { duty: 0.5, decay: 0.012 })],
    } },
    { id: 'bump', group: 'Moving around', name: 'End of list', when: 'Pressing on when there\'s nothing further.', loud: 0.4, sets: {
      air: [P('E5', 'A4', 0, 0.13, { glide: 0.035, decay: 0.04 }), N(0, 0.03, { lp: 1500, hp: 150, decay: 0.01, gain: 0.35 }), R(0.1, 0.8)],
      felt: [M('G4', 0, 0.14, { ratio: 1, index: 2.5, idecay: 0.006, decay: 0.035 }), N(0, 0.02, { lp: 900, hp: 120, decay: 0.006, gain: 0.4 }), R(0.08, 0.6)],
      glass: [F('E5', 0, 0.12, { ratio: 1, index: 1.6, idecay: 0.02, decay: 0.03 }), F('A#4', 0.05, 0.14, { ratio: 1, index: 1.6, idecay: 0.02, decay: 0.035, gain: 0.8 })],
      string: [S('E4', 0, 0.14, { bright: 0.3, damp: 0.95 }), S('B4', 0, 0.14, { bright: 0.3, damp: 0.95, gain: 0.7 })],
      chip: [Q('E5', 0, 0.08, { slideTo: 'E4', duty: 0.5, decay: 0.04 })],
    } },
    { id: 'select', group: 'Buttons', name: 'Open', when: 'A, or tapping something.', loud: 0.5, sets: {
      air: [W(0, 0.18, { from: 900, to: 5000, q: 0.9, attack: 0.05, decay: 0.06, gain: 0.35 }),
        P('B5', 'E6', 0.01, 0.16, { decay: 0.05 }), P('E6', 'B6', 0.06, 0.22, { decay: 0.08, gain: 0.9 }), R(0.18, 1)],
      felt: [M('G5', 0, 0.25, { decay: 0.08 }), M('D6', 0.06, 0.3, { decay: 0.1 }), R(0.14, 0.8)],
      glass: [F('B5', 0, 0.32, { index: 2.4, idecay: 0.05, decay: 0.11 }), F('E6', 0.055, 0.38, { index: 2.4, idecay: 0.05, decay: 0.13 })],
      string: [S('B4', 0, 0.45, { bright: 0.7, damp: 0.996 }), S('E5', 0.045, 0.5, { bright: 0.75, damp: 0.997 })],
      chip: [Q('B5', 0, 0.05, { duty: 0.25 }), Q('E6', 0.055, 0.1, { duty: 0.25, decay: 0.05 })],
    } },
    { id: 'back', group: 'Buttons', name: 'Back', when: 'B, or closing something.', loud: 0.45, sets: {
      air: [W(0, 0.16, { from: 5000, to: 900, q: 0.9, attack: 0.03, decay: 0.06, gain: 0.3 }),
        P('B6', 'E6', 0.01, 0.16, { decay: 0.05 }), P('E6', 'B5', 0.06, 0.22, { decay: 0.07, gain: 0.85 }), R(0.16, 1)],
      felt: [M('D6', 0, 0.25, { decay: 0.08 }), M('G5', 0.06, 0.3, { decay: 0.1, gain: 0.9 }), R(0.14, 0.8)],
      glass: [F('E6', 0, 0.28, { index: 2, idecay: 0.04, decay: 0.09 }), F('B5', 0.055, 0.32, { index: 1.8, idecay: 0.04, decay: 0.1 })],
      string: [S('E5', 0, 0.4, { bright: 0.6, damp: 0.995 }), S('B4', 0.045, 0.45, { bright: 0.55, damp: 0.996 })],
      chip: [Q('E6', 0, 0.05, { duty: 0.25 }), Q('B5', 0.055, 0.09, { duty: 0.25, decay: 0.045 })],
    } },
    { id: 'toggle_on', group: 'Buttons', name: 'Setting on', when: 'Turning a setting on.', loud: 0.45, sets: {
      air: [P('E6', 'B6', 0, 0.14, { glide: 0.03, decay: 0.05 }), N(0, 0.006, { lp: 10000, hp: 3000, decay: 0.0015, gain: 0.12 }), R(0.12, 0.8)],
      felt: [M('B5', 0, 0.2, { decay: 0.06 }), M('E6', 0.05, 0.28, { decay: 0.09 }), R(0.12, 0.7)],
      glass: [F('E6', 0, 0.12, { ratio: 3, index: 1.5, idecay: 0.02, decay: 0.04 }), F('G#6', 0.045, 0.28, { ratio: 3, index: 1.8, idecay: 0.03, decay: 0.09 })],
      string: [S('E5', 0, 0.09, { bright: 0.7, damp: 0.995 }), S('G#5', 0.07, 0.45, { bright: 0.35, damp: 0.996, gain: 0.75 })],
      chip: [Q('E6', 0, 0.09, { slideTo: 'B6', duty: 0.125, decay: 0.06 })],
    } },
    { id: 'toggle_off', group: 'Buttons', name: 'Setting off', when: 'Turning a setting off.', loud: 0.42, sets: {
      air: [P('B6', 'E6', 0, 0.14, { glide: 0.03, decay: 0.05 }), N(0, 0.006, { lp: 8000, hp: 2000, decay: 0.0015, gain: 0.12 }), R(0.12, 0.8)],
      felt: [M('E6', 0, 0.2, { decay: 0.06 }), M('B5', 0.05, 0.28, { decay: 0.09, gain: 0.9 }), R(0.12, 0.7)],
      glass: [F('G#6', 0, 0.12, { ratio: 3, index: 1.5, idecay: 0.02, decay: 0.04 }), F('E6', 0.045, 0.26, { ratio: 3, index: 1.3, idecay: 0.03, decay: 0.08 })],
      string: [S('G#5', 0, 0.09, { bright: 0.7, damp: 0.995 }), S('E5', 0.07, 0.4, { bright: 0.35, damp: 0.995, gain: 0.75 })],
      chip: [Q('B6', 0, 0.09, { slideTo: 'E6', duty: 0.125, decay: 0.06 })],
    } },
    { id: 'notice', group: 'Popups', name: 'Popup', when: 'News, like new achievements to download.', loud: 0.5, sets: {
      air: [W(0, 0.45, { from: 1200, to: 6000, q: 1.1, attack: 0.12, decay: 0.12, gain: 0.22 })].concat(
        pad('E6', 0.03, 0.8, { ratio: 2, index: 0.6, idecay: 0.08, attack: 0.03, decay: 0.28, gain: 0.6 }),
        [P('G#6', 'B6', 0.1, 0.5, { decay: 0.16, gain: 0.7 }), R(0.25, 1.1)]),
      felt: [M('G5', 0, 0.5, { decay: 0.18, gain: 0.8 }), M('B5', 0.07, 0.5, { decay: 0.18, gain: 0.75 }), M('D6', 0.14, 0.6, { decay: 0.22, gain: 0.75 }), R(0.2, 1)],
      glass: [F('E6', 0, 0.5, { index: 1.8, decay: 0.18 }), F('G#6', 0.07, 0.5, { index: 1.6, decay: 0.18, gain: 0.8 }), F('B6', 0.14, 0.6, { index: 1.5, decay: 0.22, gain: 0.75 })],
      string: strum(['E4', 'G#4', 'B4', 'E5'], 0, 0.02, 1.0, { bright: 0.5, damp: 0.998, gain: 0.6 }),
      chip: [Q('E6', 0, 0.09, { duty: 0.5, decay: 0.07 }), Q('G#6', 0.1, 0.2, { duty: 0.5, decay: 0.1 })],
    } },
    { id: 'error', group: 'Popups', name: 'Problem', when: 'Something went wrong.', loud: 0.5, sets: {
      air: [P('C#6', 'A5', 0, 0.16, { glide: 0.04, decay: 0.05 }), P('C#6', 'A5', 0, 0.16, { glide: 0.04, decay: 0.05, cents: 30, gain: 0.6 }),
        P('A5', 'F5', 0.15, 0.24, { glide: 0.05, decay: 0.08 }), P('A5', 'F5', 0.15, 0.24, { glide: 0.05, decay: 0.08, cents: 30, gain: 0.6 }), R(0.12, 0.9)],
      felt: [M('C5', 0, 0.2, { decay: 0.07, index: 1.8 }), M('C#5', 0, 0.2, { decay: 0.07, index: 1.8, gain: 0.8 }),
        M('A4', 0.15, 0.3, { decay: 0.1, index: 1.8 }), M('A#4', 0.15, 0.3, { decay: 0.1, index: 1.8, gain: 0.8 }), R(0.12, 0.8)],
      glass: [F('F5', 0, 0.18, { ratio: 1, index: 3.5, idecay: 0.08, decay: 0.08 }), F('B4', 0.13, 0.3, { ratio: 1, index: 3.5, idecay: 0.1, decay: 0.12 })],
      string: [S('B4', 0, 0.18, { bright: 0.35, damp: 0.97 }), S('F5', 0, 0.18, { bright: 0.35, damp: 0.97, gain: 0.7 }),
        S('B4', 0.15, 0.25, { bright: 0.35, damp: 0.97 }), S('F5', 0.15, 0.25, { bright: 0.35, damp: 0.97, gain: 0.7 })],
      chip: [Q('A4', 0, 0.1, { duty: 0.5 }), Q('G#4', 0.15, 0.18, { duty: 0.5, decay: 0.12 })],
    } },
    { id: 'saved', group: 'Popups', name: 'Saved', when: 'The share image saved to the SD card.', loud: 0.45, sets: {
      air: [N(0, 0.012, { lp: 4000, hp: 600, decay: 0.004, gain: 0.5 }), N(0.07, 0.012, { lp: 3500, hp: 500, decay: 0.004, gain: 0.4 }),
        P('E6', 'B6', 0.12, 0.25, { glide: 0.025, decay: 0.08, gain: 0.8 }), R(0.15, 0.9)],
      felt: [N(0, 0.01, { lp: 2500, hp: 300, decay: 0.003, gain: 0.5 }), N(0.07, 0.01, { lp: 2200, hp: 300, decay: 0.003, gain: 0.4 }),
        M('D6', 0.12, 0.3, { decay: 0.1, gain: 0.8 }), R(0.14, 0.8)],
      glass: [N(0, 0.02, { lp: 9000, hp: 3000, decay: 0.006 }), N(0.06, 0.025, { lp: 7000, hp: 2000, decay: 0.008, gain: 0.8 }), F('E6', 0.11, 0.3, { index: 1.6, decay: 0.1, gain: 0.7 })],
      string: [N(0, 0.02, { lp: 8000, hp: 2500, decay: 0.006 }), N(0.06, 0.025, { lp: 7000, hp: 2000, decay: 0.008, gain: 0.8 }), S('B5', 0.11, 0.4, { bright: 0.7, damp: 0.996, gain: 0.8 })],
      chip: [N(0, 0.02, { lp: 14000, hp: 5000, decay: 0.006 }), Q('E6', 0.07, 0.05, { duty: 0.25 }), Q('B6', 0.12, 0.12, { duty: 0.25, decay: 0.06 })],
    } },
    { id: 'coins', group: 'Rewards', name: 'Play Coins', when: 'Play Coins earned when the app opens.', loud: 0.55, sets: {
      air: [P('E6', 'G#6', 0, 0.12, { glide: 0.01, decay: 0.05, gain: 0.8 }), P('B6', 'C#7', 0.05, 0.12, { glide: 0.01, decay: 0.05, gain: 0.8 }),
        P('E7', 'F#7', 0.1, 0.12, { glide: 0.01, decay: 0.05, gain: 0.75 }), F('G#7', 0.15, 0.6, { ratio: 3.5, index: 0.8, idecay: 0.03, decay: 0.2, gain: 0.45 }),
        W(0.08, 0.4, { from: 3000, to: 8000, q: 1.2, attack: 0.05, decay: 0.12, gain: 0.15 }), R(0.22, 1)],
      felt: [M('G6', 0, 0.2, { decay: 0.06, index: 0.8 }), M('B6', 0.045, 0.2, { decay: 0.06, index: 0.8 }),
        M('D7', 0.09, 0.2, { decay: 0.06, index: 0.8 }), M('G7', 0.135, 0.4, { decay: 0.15, index: 0.8 }), R(0.18, 0.9)],
      glass: [F('E6', 0, 0.3, { ratio: 3.5, index: 1.8, idecay: 0.03, decay: 0.12 }), F('G#6', 0.045, 0.3, { ratio: 3.5, index: 1.8, idecay: 0.03, decay: 0.12 }),
        F('B6', 0.09, 0.3, { ratio: 3.5, index: 1.8, idecay: 0.03, decay: 0.12 }), F('E7', 0.135, 0.5, { ratio: 3.5, index: 1.8, idecay: 0.04, decay: 0.22 })],
      string: [S('B5', 0, 0.4, { bright: 0.85, damp: 0.997 }), S('E6', 0.05, 0.4, { bright: 0.85, damp: 0.997 }),
        S('G#6', 0.1, 0.4, { bright: 0.85, damp: 0.997 }), S('B6', 0.15, 0.7, { bright: 0.85, damp: 0.998 })],
      chip: [Q('E6', 0, 0.045, { duty: 0.125 }), Q('G#6', 0.045, 0.045, { duty: 0.125 }), Q('B6', 0.09, 0.045, { duty: 0.125 }), Q('E7', 0.135, 0.22, { duty: 0.125, decay: 0.12 })],
    } },
    { id: 'bought', group: 'Rewards', name: 'Bought', when: 'Buying something in the store.', loud: 0.6, sets: {
      air: [W(0, 0.25, { from: 700, to: 6000, q: 0.9, attack: 0.1, decay: 0.06, gain: 0.3 }), P('B5', 'E6', 0.12, 0.4, { decay: 0.14 }),
        P('E6', 'B6', 0.12, 0.4, { decay: 0.14, gain: 0.8 }), P('G#6', 'E7', 0.2, 0.5, { glide: 0.025, decay: 0.18, gain: 0.7 }), R(0.25, 1.1)],
      felt: [N(0, 0.012, { lp: 3000, hp: 300, decay: 0.004, gain: 0.4 }), M('G5', 0.02, 0.6, { decay: 0.2, gain: 0.7 }), M('B5', 0.02, 0.6, { decay: 0.2, gain: 0.6 }),
        M('D6', 0.02, 0.6, { decay: 0.2, gain: 0.6 }), M('G6', 0.15, 0.6, { decay: 0.22, gain: 0.7 }), R(0.2, 1)],
      glass: [N(0, 0.03, { lp: 9000, hp: 2500, decay: 0.008, gain: 0.6 }), F('E6', 0.03, 0.7, { index: 2, decay: 0.28 }), F('B6', 0.03, 0.7, { index: 2, decay: 0.28, gain: 0.8 }),
        F('E7', 0.11, 0.6, { ratio: 3.5, index: 1.5, decay: 0.25, gain: 0.6 })],
      string: [N(0, 0.03, { lp: 7000, hp: 2000, decay: 0.008, gain: 0.5 })].concat(
        strum(['E5', 'G#5', 'B5', 'E6'], 0.03, 0.015, 0.9, { bright: 0.7, damp: 0.998, gain: 0.6 }),
        [S('B6', 0.2, 0.8, { bright: 0.9, damp: 0.998, gain: 0.7 })]),
      chip: [N(0, 0.025, { lp: 12000, hp: 4000, decay: 0.006, gain: 0.5 }), Q('B5', 0.02, 0.05, { duty: 0.25 }), Q('E6', 0.07, 0.05, { duty: 0.25 }),
        Q('G#6', 0.12, 0.05, { duty: 0.25 }), Q('B6', 0.17, 0.28, { duty: 0.25, decay: 0.15 })],
    } },
    { id: 'complete', group: 'Rewards', name: 'Game complete', when: 'Opening a game with every achievement unlocked.', loud: 0.7, sets: {
      air: [W(0, 0.6, { from: 500, to: 7000, q: 1, attack: 0.35, decay: 0.15, gain: 0.3 }),
        P('E5', 'B5', 0.25, 0.3, { decay: 0.12, gain: 0.7 }), P('B5', 'E6', 0.35, 0.3, { decay: 0.12, gain: 0.7 }),
        P('E6', 'G#6', 0.45, 0.3, { decay: 0.12, gain: 0.7 }), P('G#6', 'B6', 0.55, 0.3, { decay: 0.12, gain: 0.7 })].concat(
        pad('E6', 0.6, 1.4, { ratio: 2, index: 0.5, idecay: 0.2, attack: 0.08, decay: 0.5, gain: 0.5 }),
        [F('B6', 0.6, 1.4, { ratio: 2, index: 0.5, idecay: 0.2, attack: 0.08, decay: 0.5, gain: 0.4 }),
          F('G#6', 0.6, 1.4, { ratio: 2, index: 0.5, idecay: 0.2, attack: 0.08, decay: 0.5, gain: 0.35 }),
          F('E7', 0.7, 1.0, { ratio: 3.5, index: 1, idecay: 0.04, decay: 0.3, gain: 0.35 }), R(0.3, 1.2)]),
      felt: [M('G4', 0, 0.4, { decay: 0.15, gain: 0.7 }), M('B4', 0.08, 0.4, { decay: 0.15, gain: 0.7 }), M('D5', 0.16, 0.4, { decay: 0.15, gain: 0.7 }),
        M('G5', 0.24, 0.4, { decay: 0.15, gain: 0.7 }), M('B5', 0.32, 0.4, { decay: 0.15, gain: 0.7 }), M('D6', 0.4, 0.5, { decay: 0.18, gain: 0.7 }),
        F('G5', 0.5, 1.4, { ratio: 2, index: 0.4, idecay: 0.2, attack: 0.06, decay: 0.5, gain: 0.5 }),
        F('D6', 0.5, 1.4, { ratio: 2, index: 0.4, idecay: 0.2, attack: 0.06, decay: 0.5, gain: 0.4 }),
        M('G6', 0.5, 1.0, { decay: 0.35, gain: 0.8 }), R(0.28, 1.2)],
      glass: [F('E5', 0, 0.4, { index: 2, decay: 0.2, gain: 0.8 }), F('G#5', 0.09, 0.4, { index: 2, decay: 0.2, gain: 0.8 }),
        F('B5', 0.18, 0.4, { index: 2, decay: 0.2, gain: 0.8 }), F('E6', 0.27, 0.5, { index: 2, decay: 0.2, gain: 0.8 }),
        F('E6', 0.45, 1.3, { index: 2.2, idecay: 0.08, decay: 0.5 }), F('G#6', 0.45, 1.3, { index: 2.2, idecay: 0.08, decay: 0.5, gain: 0.8 }),
        F('B6', 0.45, 1.3, { index: 2.2, idecay: 0.08, decay: 0.5, gain: 0.8 }), F('E7', 0.6, 1.0, { ratio: 3.5, index: 1.5, decay: 0.35, gain: 0.5 })],
      string: [].concat(
        strum(['E4', 'B4', 'E5', 'G#5'], 0, 0.018, 0.45, { bright: 0.65, damp: 0.997, gain: 0.55 }),
        strum(['A4', 'C#5', 'E5', 'A5'], 0.38, 0.018, 0.45, { bright: 0.65, damp: 0.997, gain: 0.55 }),
        strum(['B4', 'E5', 'G#5', 'B5', 'E6'], 0.76, 0.02, 1.4, { bright: 0.7, damp: 0.999, gain: 0.55 })),
      chip: [Q('E5', 0, 0.08, { duty: 0.25 }), Q('G#5', 0.1, 0.08, { duty: 0.25 }), Q('B5', 0.2, 0.08, { duty: 0.25 }), Q('E6', 0.3, 0.14, { duty: 0.25 }),
        Q('D#6', 0.5, 0.1, { duty: 0.25 }), Q('E6', 0.62, 0.55, { duty: 0.25, decay: 0.3 }),
        Q('B5', 0.62, 0.55, { duty: 0.5, decay: 0.3, gain: 0.5 }), Q('G#5', 0.62, 0.55, { duty: 0.5, decay: 0.3, gain: 0.4 })],
    } },
  ];

  const envelope = (t, v) => {
    const a = t < v.attack ? t / v.attack : 1;
    const d = v.decay > 0 ? Math.exp(-Math.max(0, t - v.attack) / v.decay) : 1;
    const left = v.dur - t;
    const r = left < v.release ? Math.max(0, left / v.release) : 1;
    return a * d * r;
  };
  const freqAt = (v, t) => (v.f1 === v.f ? v.f : v.f * Math.pow(v.f1 / v.f, Math.min(1, t / (v.glide || v.dur))));
  const blep = (p, dt) => {
    if (p < dt) { p /= dt; return p + p - p * p - 1; }
    if (p > 1 - dt) { p = (p - 1) / dt; return p * p + p + p + 1; }
    return 0;
  };

  const KINDS = {
    fm(out, sr, v) {
      const n = Math.round(v.dur * sr), s0 = Math.round(v.t * sr);
      let pc = 0, pm = 0;
      for (let i = 0; i < n && s0 + i < out.length; i++) {
        const t = i / sr, f = freqAt(v, t);
        const index = v.index * Math.exp(-t / v.idecay);
        out[s0 + i] += v.gain * envelope(t, v) * Math.sin(pc + index * Math.sin(pm));
        pc += 2 * Math.PI * f / sr;
        pm += 2 * Math.PI * f * v.ratio / sr;
      }
    },
    pulse(out, sr, v) {
      const n = Math.round(v.dur * sr), s0 = Math.round(v.t * sr);
      let phase = 0;
      for (let i = 0; i < n && s0 + i < out.length; i++) {
        const t = i / sr, dt = freqAt(v, t) / sr;
        let x = phase < v.duty ? 1 : -1;
        x += blep(phase, dt);
        x -= blep((phase + 1 - v.duty) % 1, dt);
        x -= 2 * v.duty - 1;
        out[s0 + i] += 0.5 * v.gain * envelope(t, v) * x;
        phase += dt;
        if (phase >= 1) phase -= 1;
      }
    },
    pluck(out, sr, v) {
      const n = Math.round(v.dur * sr), s0 = Math.round(v.t * sr);
      const period = sr / v.f, delay = period - 0.5;
      const y = new Float32Array(n);
      const r = rng(v.seed);
      const burst = Math.min(n, Math.ceil(period));
      const norm = Math.sqrt((2 - v.bright) / v.bright);
      let lp = 0, mean = 0;
      for (let i = 0; i < burst; i++) { lp += v.bright * (r() - lp); y[i] = lp * norm; mean += y[i] / burst; }
      for (let i = 0; i < burst; i++) y[i] -= mean;   // the string loop keeps any offset forever
      for (let i = burst; i < n; i++) {
        const pos = i - delay, k = Math.floor(pos), fr = pos - k;
        const a = y[k] + (y[k + 1] - y[k]) * fr;
        const b = k >= 1 ? y[k - 1] + (y[k] - y[k - 1]) * fr : 0;
        y[i] = v.damp * 0.5 * (a + b);
      }
      for (let i = 0; i < n && s0 + i < out.length; i++) out[s0 + i] += v.gain * envelope(i / sr, v) * y[i];
    },
    noise(out, sr, v) {
      const n = Math.round(v.dur * sr), s0 = Math.round(v.t * sr);
      const r = rng(v.seed);
      const al = 1 - Math.exp(-2 * Math.PI * Math.min(v.lp, sr * 0.45) / sr);
      const ah = 1 - Math.exp(-2 * Math.PI * Math.min(v.hp, sr * 0.4) / sr);
      let lp = 0, hp = 0;
      for (let i = 0; i < n && s0 + i < out.length; i++) {
        lp += al * (r() - lp);
        hp += ah * (lp - hp);
        out[s0 + i] += 2 * v.gain * envelope(i / sr, v) * (lp - hp);
      }
    },
    whoosh(out, sr, v) {
      // Noise through a state-variable band-pass whose centre sweeps from v.from to v.to. The
      // filter runs twice per sample, and its coefficient stays under 1.9 - q, where it's stable.
      const n = Math.round(v.dur * sr), s0 = Math.round(v.t * sr);
      const r = rng(v.seed);
      let low = 0, band = 0;
      for (let i = 0; i < n && s0 + i < out.length; i++) {
        const t = i / sr;
        const fc = Math.min(v.from * Math.pow(v.to / v.from, Math.min(1, t / (v.glide || v.dur))), sr * 0.2);
        const f = Math.min(2 * Math.sin(Math.PI * fc / (2 * sr)), 1.9 - v.q);
        const x = r();
        for (let k = 0; k < 2; k++) {
          low += f * band;
          band += f * (x - low - v.q * band);
        }
        out[s0 + i] += 0.5 * v.gain * envelope(t, v) * band;
      }
    },
  };

  // A small room: four damped comb filters, then two all-passes (Schroeder), mixed in.
  function addSpace(x, sr, mix, size) {
    if (!(mix > 0)) return;
    const wet = new Float32Array(x.length);
    for (const s of [0.0297, 0.0371, 0.0411, 0.0437]) {
      const d = Math.max(1, Math.round(s * size * sr));
      const buf = new Float32Array(d);
      let k = 0, lp = 0;
      for (let i = 0; i < x.length; i++) {
        const y = buf[k];
        lp = y * 0.65 + lp * 0.35;
        buf[k] = x[i] + lp * 0.72;
        wet[i] += y * 0.25;
        k = (k + 1) % d;
      }
    }
    for (const s of [0.005, 0.0017]) {
      const d = Math.max(1, Math.round(s * sr));
      const buf = new Float32Array(d);
      let k = 0;
      for (let i = 0; i < wet.length; i++) {
        const v = wet[i], y = buf[k] - 0.5 * v;
        buf[k] = v + 0.5 * y;
        wet[i] = y;
        k = (k + 1) % d;
      }
    }
    for (let i = 0; i < x.length; i++) x[i] += mix * wet[i];
  }

  function shapeTone(x, sr, tone) {
    if (!tone) return;
    if (tone < 0) {
      const cut = Math.min(16000 * Math.pow(2, tone * 3.7), sr * 0.45);
      const a = 1 - Math.exp(-2 * Math.PI * cut / sr);
      for (let pass = 0; pass < 2; pass++) {
        let s = 0;
        for (let i = 0; i < x.length; i++) { s += a * (x[i] - s); x[i] = s; }
      }
    } else {
      const a = 1 - Math.exp(-2 * Math.PI * 2500 / sr);
      let s = 0;
      for (let i = 0; i < x.length; i++) { s += a * (x[i] - s); x[i] += tone * 1.6 * (x[i] - s); }
    }
  }
  function blockDc(x, sr) {
    const R = 1 - 2 * Math.PI * 20 / sr;
    let px = 0, py = 0;
    for (let i = 0; i < x.length; i++) { const y = x[i] - px + R * py; px = x[i]; py = y; x[i] = y; }
  }
  function addEcho(x, sr, mix) {
    if (!(mix > 0)) return;
    const d = Math.round(0.12 * sr), fb = 0.35;
    const w = new Float32Array(x.length);
    for (let i = 0; i < x.length; i++) {
      const del = i >= d ? w[i - d] : 0;
      w[i] = x[i] + fb * del;
      x[i] += mix * del;
    }
  }
  // Level by the loudest 15 ms, so sharp and soft sounds come out about as loud as each other.
  function level(x, sr, target) {
    const win = Math.max(1, Math.round(0.015 * sr));
    let sum = 0, best = 0;
    for (let i = 0; i < x.length; i++) {
      sum += x[i] * x[i];
      if (i >= win) sum -= x[i - win] * x[i - win];
      if (sum > best) best = sum;
    }
    const rms = Math.sqrt(best / win);
    if (rms < 1e-9) return;
    const gain = target * 0.4 / rms;
    for (let i = 0; i < x.length; i++) {
      let v = x[i] * gain;
      const a = Math.abs(v);
      if (a > 0.9) v = Math.sign(v) * (0.9 + 0.1 * Math.tanh((a - 0.9) / 0.1));
      x[i] = v;
    }
  }
  function trim(x, sr) {
    let last = x.length - 1;
    while (last > 0 && Math.abs(x[last]) < 0.0005) last--;
    const n = Math.min(x.length, last + 1 + Math.round(0.005 * sr));
    const y = x.slice(0, n);
    const fade = Math.min(n, Math.round(0.005 * sr));
    for (let i = 0; i < fade; i++) y[n - 1 - i] *= i / fade;
    return y;
  }

  const TWEAKS = { pitch: 0, length: 1, volume: 1, tone: 0, echo: 0 };

  function render(effectId, family, tweaks, sr) {
    const effect = EFFECTS.find((e) => e.id === effectId);
    const tw = Object.assign({}, TWEAKS, tweaks);
    const pf = Math.pow(2, tw.pitch / 12), ts = tw.length;
    const base = hash(effectId + '/' + family);
    const recipe = effect.sets[family];
    const space = recipe.find((v) => v.kind === 'space');
    const voices = recipe.filter((v) => v.kind !== 'space').map((raw, i) => {
      const v = Object.assign({}, DEFAULTS[raw.kind], raw);
      v.t *= ts; v.dur *= ts; v.attack *= ts; v.decay *= ts;
      v.release = Math.max(0.004, v.release * ts);
      if (v.idecay) v.idecay *= ts;
      if (v.glide) v.glide *= ts;
      if (v.note) { v.f = hz(v.note) * pf; v.f1 = v.slideTo ? hz(v.slideTo) * pf : v.f; }
      if (v.cents) { const c = Math.pow(2, v.cents / 1200); v.f *= c; v.f1 *= c; }
      if (v.kind === 'noise') { v.lp *= pf; v.hp *= pf; }
      if (v.kind === 'whoosh') { v.from *= pf; v.to *= pf; }
      if (v.kind === 'pluck') v.damp = Math.pow(v.damp, 1 / ts);
      v.seed = (base + 7919 * (i + 1)) >>> 0;
      return v;
    });
    const end = Math.max.apply(null, voices.map((v) => v.t + v.dur));
    const tail = Math.max(tw.echo > 0 ? 0.7 : 0.02, space ? 0.9 : 0);
    const out = new Float32Array(Math.ceil((end + tail) * sr));
    for (const v of voices) KINDS[v.kind](out, sr, v);
    blockDc(out, sr);
    if (space) addSpace(out, sr, space.mix, space.size || 1);
    shapeTone(out, sr, tw.tone);
    addEcho(out, sr, tw.echo);
    level(out, sr, effect.loud * tw.volume);
    return trim(out, sr);
  }

  return { EFFECTS, FAMILIES, TWEAKS, render };
})();

if (typeof module !== 'undefined') module.exports = Synth;
