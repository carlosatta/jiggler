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
