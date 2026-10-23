// Run with: node --test web/test
const test = require('node:test');
const assert = require('node:assert/strict');
const JigLib = require('../lib.js');

test('fmtDuration counts down as m:ss and h:mm:ss', () => {
  assert.equal(JigLib.fmtDuration(0), '0:00');
  assert.equal(JigLib.fmtDuration(999), '0:01'); // rounds up: never shows 0:00 while time remains
  assert.equal(JigLib.fmtDuration(59000), '0:59');
  assert.equal(JigLib.fmtDuration(61000), '1:01');
  assert.equal(JigLib.fmtDuration(3600000), '1:00:00');
  assert.equal(JigLib.fmtDuration(3661000), '1:01:01');
});

test('fmtAgo speaks Italian and never says 0 minuti', () => {
  assert.equal(JigLib.fmtAgo(0), 'meno di un minuto fa');
  assert.equal(JigLib.fmtAgo(59999), 'meno di un minuto fa');
  assert.equal(JigLib.fmtAgo(60000), '1 minuto fa');
  assert.equal(JigLib.fmtAgo(185000), '3 minuti fa');
});

test('hex4 pads to four hex digits', () => {
  assert.equal(JigLib.hex4(0x46d), '0x046D');
  assert.equal(JigLib.hex4(0), '0x0000');
  assert.equal(JigLib.hex4(0xb034), '0xB034');
});

test('tr translates known server messages and falls back sensibly', () => {
  assert.equal(JigLib.tr('unknown profile'), 'Profilo sconosciuto.');
  assert.equal(JigLib.tr('something unexpected'), 'something unexpected');
  assert.equal(JigLib.tr(''), 'Valore non valido.');
  assert.equal(JigLib.tr(undefined), 'Valore non valido.');
});

test('every translation is a non-empty Italian sentence ending with a full stop', () => {
  for (const [english, italian] of Object.entries(JigLib.MESSAGES)) {
    assert.ok(english.length > 0 && italian.length > 0, english);
    assert.ok(italian.endsWith('.'), `"${italian}" should end with a full stop`);
    assert.notEqual(english, italian);
  }
});

const bt = (over) => Object.assign({
  connected: false, held: false, peerCount: 1, computer: null,
  pairing: { open: false, remainingMs: null }
}, over);
const device = { bleName: 'MX Master 3S' };

test('fmtSince reads as a duration', () => {
  assert.equal(JigLib.fmtSince(5000), 'meno di un minuto');
  assert.equal(JigLib.fmtSince(60000), '1 minuto');
  assert.equal(JigLib.fmtSince(25 * 60000), '25 minuti');
  assert.equal(JigLib.fmtSince(60 * 60000), '1 ora');
  assert.equal(JigLib.fmtSince(65 * 60000), '1 ora e 5 min');
  assert.equal(JigLib.fmtSince(3 * 3600000), '3 ore');
});

test('connection view: connected names the computer and offers disconnect', () => {
  const v = JigLib.connectionView(bt({ connected: true, computer: { label: 'Computer 2', connectedForMs: 12 * 60000 } }), device);
  assert.equal(v.tone, 'ok');
  assert.equal(v.title, 'Collegato a Computer 2');
  assert.match(v.detail, /da 12 minuti/);
  assert.deepEqual(v.actions, ['disconnect']);
});

test('connection view: held explains the device is invisible and offers reconnect', () => {
  const v = JigLib.connectionView(bt({ held: true }), device);
  assert.equal(v.tone, 'warn');
  assert.equal(v.title, 'Scollegato');
  assert.match(v.detail, /invisibile/);
  assert.deepEqual(v.actions, ['reconnect']);
});

test('connection view: first run tells the owner what to pick on the computer', () => {
  const v = JigLib.connectionView(bt({ peerCount: 0, pairing: { open: true, remainingMs: null } }), device);
  assert.equal(v.tone, 'open');
  assert.equal(v.title, 'Pronto per l’abbinamento');
  assert.match(v.detail, /«MX Master 3S»/);
  assert.deepEqual(v.actions, []);
});

test('connection view: a timed window shows the countdown and can be closed', () => {
  const v = JigLib.connectionView(bt({ pairing: { open: true, remainingMs: 92000 } }), device);
  assert.equal(v.title, 'Abbinamento aperto');
  assert.match(v.detail, /1:32/);
  assert.deepEqual(v.actions, ['closePairing']);
});

test('connection view: paired but not connected', () => {
  const v = JigLib.connectionView(bt(), device);
  assert.equal(v.tone, 'idle');
  assert.equal(v.title, 'Nessun computer collegato');
  assert.deepEqual(v.actions, ['pair']);
});

test('connection view: connected while a window is open keeps disconnect and mentions the window', () => {
  const v = JigLib.connectionView(
    bt({ connected: true, computer: { label: 'Computer 1', connectedForMs: 1000 }, pairing: { open: true, remainingMs: 30000 } }), device);
  assert.deepEqual(v.actions, ['disconnect', 'closePairing']);
});

test('checkNewToken validates length, spaces and confirmation', () => {
  assert.equal(JigLib.checkNewToken('abcdefgh', 'abcdefgh'), '');
  assert.equal(JigLib.checkNewToken('short', 'short'), 'Da 8 a 40 caratteri, senza spazi.');
  assert.equal(JigLib.checkNewToken('has space 123', 'has space 123'), 'Da 8 a 40 caratteri, senza spazi.');
  assert.equal(JigLib.checkNewToken('a'.repeat(41), 'a'.repeat(41)), 'Da 8 a 40 caratteri, senza spazi.');
  assert.equal(JigLib.checkNewToken('abcdefgh', 'abcdefgX'), 'I due token non coincidono.');
});

test('server messages added for Bluetooth and token are translated', () => {
  for (const m of ['the pairing window must last between 30 and 600 seconds', 'no such paired computer',
    'the name must be 1 to 32 characters', 'the current token is not correct',
    'must be 8 to 40 printable characters without spaces', 'could not save the token (storage full?)']) {
    assert.notEqual(JigLib.tr(m), m, m);
  }
});
