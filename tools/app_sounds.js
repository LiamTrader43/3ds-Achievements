// Renders the app's sound sets, listed in app/sounds.json, through tools/sound_synth.js.
//
//     node tools/app_sounds.js [sounds.json]
//
// sounds.json: "default" (the set the app starts with) and "sets", each with an id (a word: it's
// what /achievements/sounds.txt holds), a name and description for the settings, a "take" used
// for every sound, and optional "sounds" for single sounds: { "<sound>": { "sound": "<take>" or
// "none", "pitch": ..., ... } }, the shape the Sound Lab page's "Copy my choices" gives.
//
// Writes app/data/sounds.bin (every set's sounds one after another: 16-bit little-endian mono at
// the 3DS sound hardware's rate, each padded to 16 samples) and app/source/sounds.h (the SoundId
// list, the sets, and where each sound starts). Both are committed, so building the app doesn't
// need Node.
'use strict';
const fs = require('fs');
const path = require('path');
const Synth = require('./sound_synth.js');

const RATE = 32728;            // the DSP's output rate (32728.5 Hz): no resampling on the console
const ALIGN = 16;              // samples: each sound starts 32 bytes after the last
const ROOT = path.join(__dirname, '..');
const listPath = process.argv[2] || path.join(ROOT, 'app', 'sounds.json');

const fail = (text) => { throw new Error(listPath + ': ' + text); };
const list = JSON.parse(fs.readFileSync(listPath, 'utf8'));
if (!list || !Array.isArray(list.sets) || list.sets.length === 0)
    fail('no "sets"');
const takes = Synth.FAMILIES.map((f) => f.id);
const effects = Synth.EFFECTS.map((e) => e.id);
const ids = list.sets.map((s) => s.id);
for (const s of list.sets) {
    if (typeof s.id !== 'string' || !/^[a-z0-9_-]{1,15}$/.test(s.id))
        fail('a set\'s id must be 1-15 of a-z 0-9 _ -');
    if (ids.indexOf(s.id) !== ids.lastIndexOf(s.id))
        fail('two sets are called "' + s.id + '"');
    if (typeof s.name !== 'string' || s.name.length < 1 || s.name.length > 15 || /[^\x20-\x7e]/.test(s.name))
        fail(s.id + ': its name must be 1-15 plain characters');
    if (typeof s.description !== 'string' || s.description.length > 95 || /[^\x20-\x7e]/.test(s.description))
        fail(s.id + ': its description must be at most 95 plain characters');
    if (!takes.includes(s.take))
        fail(s.id + ': unknown take "' + s.take + '" (' + takes.join(', ') + ')');
    for (const id of Object.keys(s.sounds || {}))
        if (!effects.includes(id))
            fail(s.id + ': unknown sound "' + id + '"');
}
const defaultSet = ids.indexOf(list.default);
if (defaultSet < 0)
    fail('"default" must be one of the sets (' + ids.join(', ') + ')');

const c = (text) => JSON.stringify(text);       // a C string literal (plain ASCII only, checked above)
const parts = [], sets = [];
let at = 0;
for (const s of list.sets) {
    const start = at, sounds = [];
    for (const e of Synth.EFFECTS) {
        const one = (s.sounds || {})[e.id] || {};
        const take = one.sound || s.take;
        let samples = new Float32Array(0);
        if (take !== 'none') {
            if (!takes.includes(take))
                fail(s.id + '/' + e.id + ': unknown take "' + take + '"');
            const tweaks = {};
            for (const k of Object.keys(Synth.TWEAKS))
                if (one[k] !== undefined) {
                    if (typeof one[k] !== 'number' || !Number.isFinite(one[k]))
                        fail(s.id + '/' + e.id + ': ' + k + ' must be a number');
                    tweaks[k] = one[k];
                }
            samples = Synth.render(e.id, take, tweaks, RATE);
        }
        const padded = Math.ceil(samples.length / ALIGN) * ALIGN;
        const buf = Buffer.alloc(padded * 2);
        samples.forEach((v, i) => buf.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(v * 32767))), 2 * i));
        parts.push(buf);
        sounds.push({ id: e.id, name: e.name, take, start: at - start, length: samples.length });
        at += padded;
    }
    sets.push({ set: s, start, size: at - start, sounds });
}

const bin = Buffer.concat(parts);
fs.mkdirSync(path.join(ROOT, 'app', 'data'), { recursive: true });
fs.writeFileSync(path.join(ROOT, 'app', 'data', 'sounds.bin'), bin);

const enumName = (id) => 'SOUND_' + id.toUpperCase();
const width = Math.max(...effects.map((id) => enumName(id).length));
const L = [];
L.push('// Made by tools/app_sounds.js from app/sounds.json: don\'t edit, run it again. The app\'s sound');
L.push('// sets, each a full set of sounds, 16-bit mono at SOUND_RATE Hz, one after another in');
L.push('// data/sounds.bin (sounds_bin.h). A length of 0: that set has no sound for it.');
L.push('#ifndef SOUNDS_H');
L.push('#define SOUNDS_H');
L.push('');
L.push('#define SOUND_RATE          ' + RATE);
L.push('#define SOUND_SET_COUNT     ' + sets.length);
L.push('#define SOUND_SET_DEFAULT   ' + defaultSet + '       // ' + list.sets[defaultSet].name);
L.push('');
L.push('typedef enum');
L.push('{');
for (const e of Synth.EFFECTS)
    L.push('    ' + (enumName(e.id) + ',').padEnd(width + 2) + '// ' + e.name);
L.push('    SOUND_COUNT');
L.push('} SoundId;');
L.push('');
L.push('#ifdef SOUND_TABLES');
L.push('// Each set: its id (what /achievements/sounds.txt holds), name and description; where its');
L.push('// sounds start in data/sounds.bin and how much they take, in samples.');
L.push('static const char *const SOUND_SET_ID[SOUND_SET_COUNT] = { ' + sets.map((s) => c(s.set.id)).join(', ') + ' };');
L.push('static const char *const SOUND_SET_NAME[SOUND_SET_COUNT] = { ' + sets.map((s) => c(s.set.name)).join(', ') + ' };');
L.push('static const char *const SOUND_SET_DESCRIPTION[SOUND_SET_COUNT] =');
L.push('{');
for (const s of sets)
    L.push('    ' + c(s.set.description) + ',');
L.push('};');
L.push('static const unsigned int SOUND_SET_START[SOUND_SET_COUNT] = { ' + sets.map((s) => s.start).join(', ') + ' };');
L.push('static const unsigned int SOUND_SET_SIZE[SOUND_SET_COUNT] = { ' + sets.map((s) => s.size).join(', ') + ' };');
L.push('// Each sound in its set: where it starts (from the set\'s start) and its length, in samples.');
L.push('static const unsigned int SOUND_START[SOUND_SET_COUNT][SOUND_COUNT] =');
L.push('{');
for (const s of sets)
    L.push('    { ' + s.sounds.map((x) => x.start).join(', ') + ' },     // ' + s.set.name);
L.push('};');
L.push('static const unsigned int SOUND_LENGTH[SOUND_SET_COUNT][SOUND_COUNT] =');
L.push('{');
for (const s of sets)
    L.push('    { ' + s.sounds.map((x) => x.length).join(', ') + ' },');
L.push('};');
L.push('#endif');
L.push('');
L.push('#endif');
fs.writeFileSync(path.join(ROOT, 'app', 'source', 'sounds.h'), L.join('\n') + '\n');

for (const s of sets)
    console.log(s.set.id.padEnd(8) + (s.size * 2 / 1024).toFixed(0).padStart(5) + ' KB' + (s.set.id === list.default ? '  (default)' : ''));
console.log(sets.length + ' sets, ' + (bin.length / 1024).toFixed(0) + ' KB: app/data/sounds.bin, app/source/sounds.h');
