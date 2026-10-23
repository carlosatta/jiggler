// Run with: node --test web/test/*.test.js
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

test('fmtAgo never says 0 minutes', () => {
  assert.equal(JigLib.fmtAgo(0), 'less than a minute ago');
  assert.equal(JigLib.fmtAgo(59999), 'less than a minute ago');
  assert.equal(JigLib.fmtAgo(60000), '1 minute ago');
  assert.equal(JigLib.fmtAgo(185000), '3 minutes ago');
});

test('fmtSince reads as a duration', () => {
  assert.equal(JigLib.fmtSince(5000), 'less than a minute');
  assert.equal(JigLib.fmtSince(60000), '1 minute');
  assert.equal(JigLib.fmtSince(25 * 60000), '25 minutes');
  assert.equal(JigLib.fmtSince(60 * 60000), '1 hour');
  assert.equal(JigLib.fmtSince(65 * 60000), '1 hour 5 min');
  assert.equal(JigLib.fmtSince(3 * 3600000), '3 hours');
});

test('hex4 pads to four hex digits', () => {
  assert.equal(JigLib.hex4(0x46d), '0x046D');
  assert.equal(JigLib.hex4(0), '0x0000');
  assert.equal(JigLib.hex4(0xb034), '0xB034');
});

test('tr turns a server message into a sentence', () => {
  assert.equal(JigLib.tr('must be between 5 and 3600'), 'Must be between 5 and 3600.');
  assert.equal(JigLib.tr('unknown profile'), 'Unknown profile.');
  assert.equal(JigLib.tr('Already a sentence.'), 'Already a sentence.');
  assert.equal(JigLib.tr(''), 'Invalid value.');
  assert.equal(JigLib.tr(undefined), 'Invalid value.');
});

const bt = (over) => Object.assign({
  connected: false, held: false, heldRemainingMs: null, peerCount: 1, computer: null,
  pairing: { open: false, remainingMs: null }
}, over);
const device = { bleName: 'MX Master 3S' };

test('connection view: connected names the computer and offers disconnect', () => {
  const v = JigLib.connectionView(bt({ connected: true, computer: { label: 'Computer 2', connectedForMs: 12 * 60000 } }), device);
  assert.equal(v.tone, 'ok');
  assert.equal(v.title, 'Connected to Computer 2');
  assert.match(v.detail, /For 12 minutes/);
  assert.deepEqual(v.actions, ['disconnect']);
});

test('connection view: held shows the countdown and offers reconnect', () => {
  const v = JigLib.connectionView(bt({ held: true, heldRemainingMs: 92000 }), device);
  assert.equal(v.tone, 'warn');
  assert.equal(v.title, 'Disconnected');
  assert.match(v.detail, /1:32/);
  assert.match(v.detail, /hidden/i);
  assert.deepEqual(v.actions, ['reconnect']);
});

test('connection view: first run tells the owner what to pick on the computer', () => {
  const v = JigLib.connectionView(bt({ peerCount: 0, pairing: { open: true, remainingMs: null } }), device);
  assert.equal(v.tone, 'open');
  assert.equal(v.title, 'Ready to pair');
  assert.match(v.detail, /“MX Master 3S”/);
  assert.deepEqual(v.actions, []);
});

test('connection view: a timed window shows the countdown and can be stopped', () => {
  const v = JigLib.connectionView(bt({ pairing: { open: true, remainingMs: 92000 } }), device);
  assert.equal(v.title, 'Pairing is open');
  assert.match(v.detail, /1:32/);
  assert.deepEqual(v.actions, ['closePairing']);
});

test('connection view: paired but not connected offers to pair another computer', () => {
  const v = JigLib.connectionView(bt(), device);
  assert.equal(v.tone, 'idle');
  assert.equal(v.title, 'No computer connected');
  assert.deepEqual(v.actions, ['pair']);
});

test('connection view: connected while a window is open keeps disconnect and offers to stop pairing', () => {
  const v = JigLib.connectionView(
    bt({ connected: true, computer: { label: 'Computer 1', connectedForMs: 1000 }, pairing: { open: true, remainingMs: 30000 } }), device);
  assert.deepEqual(v.actions, ['disconnect', 'closePairing']);
});

test('checkNewToken validates length, spaces and confirmation', () => {
  assert.equal(JigLib.checkNewToken('abcdefgh', 'abcdefgh'), '');
  assert.equal(JigLib.checkNewToken('short', 'short'), '8 to 40 characters, no spaces.');
  assert.equal(JigLib.checkNewToken('has space 123', 'has space 123'), '8 to 40 characters, no spaces.');
  assert.equal(JigLib.checkNewToken('a'.repeat(41), 'a'.repeat(41)), '8 to 40 characters, no spaces.');
  assert.equal(JigLib.checkNewToken('abcdefgh', 'abcdefgX'), 'The two tokens do not match.');
});

// --- run for / until a time ---

test('minutesUntil gives minutes to the next occurrence of a clock time', () => {
  const at = (h, m, s = 0) => new Date(2026, 9, 23, h, m, s); // local time
  assert.equal(JigLib.minutesUntil('15:40', at(14, 30)), 70);
  assert.equal(JigLib.minutesUntil('14:31', at(14, 30, 30)), 1);   // rounds up
  assert.equal(JigLib.minutesUntil('08:00', at(14, 30)), 17 * 60 + 30); // already passed today: tomorrow
  assert.equal(JigLib.minutesUntil('14:30', at(14, 30, 0)), 1440);   // right now means the same time tomorrow
});

test('minutesUntil refuses malformed input', () => {
  assert.equal(JigLib.minutesUntil('', new Date()), null);
  assert.equal(JigLib.minutesUntil('25:00', new Date()), null);
  assert.equal(JigLib.minutesUntil('9am', new Date()), null);
});

test('runForMinutes understands the presets, a clock time and a custom value', () => {
  const now = new Date(2026, 9, 23, 14, 30);
  assert.equal(JigLib.runForMinutes({ choice: 'forever' }, now), 0);          // 0 = no limit
  assert.equal(JigLib.runForMinutes({ choice: '5' }, now), 5);
  assert.equal(JigLib.runForMinutes({ choice: '30' }, now), 30);
  assert.equal(JigLib.runForMinutes({ choice: 'until', time: '15:00' }, now), 30);
  assert.equal(JigLib.runForMinutes({ choice: 'custom', custom: '45' }, now), 45);
  assert.equal(JigLib.runForMinutes({ choice: 'custom', custom: '0' }, now), null);
  assert.equal(JigLib.runForMinutes({ choice: 'custom', custom: '1441' }, now), null);
  assert.equal(JigLib.runForMinutes({ choice: 'custom', custom: 'abc' }, now), null);
  assert.equal(JigLib.runForMinutes({ choice: 'custom', custom: '12.5' }, now), null);
  assert.equal(JigLib.runForMinutes({ choice: 'until', time: '' }, now), null);
});

test('stopsAt formats the wall-clock moment a timed run ends', () => {
  const now = new Date(2026, 9, 23, 14, 30, 0);
  assert.equal(JigLib.stopsAt(10 * 60000, now), '14:40');
  assert.equal(JigLib.stopsAt(10 * 3600000, now), '00:30 tomorrow');
});

// --- schedules ---

test('describeDays groups days the way people say them', () => {
  assert.equal(JigLib.describeDays(['mon', 'tue', 'wed', 'thu', 'fri']), 'Mon–Fri');
  assert.equal(JigLib.describeDays(['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun']), 'Every day');
  assert.equal(JigLib.describeDays(['sat', 'sun']), 'Sat, Sun');
  assert.equal(JigLib.describeDays(['mon', 'wed', 'fri']), 'Mon, Wed, Fri');
  assert.equal(JigLib.describeDays(['tue', 'wed', 'thu']), 'Tue–Thu');
  assert.equal(JigLib.describeDays([]), 'No days');
});

test('scheduleEnd adds the duration to the start and notes the next day', () => {
  assert.equal(JigLib.scheduleEnd('09:00', 480), '17:00');
  assert.equal(JigLib.scheduleEnd('22:00', 480), '06:00 next day');
  assert.equal(JigLib.scheduleEnd('00:00', 1440), '00:00 next day');
  assert.equal(JigLib.scheduleEnd('23:59', 1), '00:00 next day');
});

test('scheduleSummary is one readable line', () => {
  assert.equal(JigLib.scheduleSummary({ days: ['mon', 'tue', 'wed', 'thu', 'fri'], start: '09:00', durationMin: 480 }), 'Mon–Fri, 09:00 to 17:00');
  assert.equal(JigLib.scheduleSummary({ days: ['fri'], start: '22:00', durationMin: 480 }), 'Fri, 22:00 to 06:00 next day');
});

test('every server message the page may show reads as a sentence', () => {
  for (const m of ['the pairing window must last between 30 and 600 seconds', 'no such paired computer',
    'the name must be 1 to 32 characters', 'the current token is not correct',
    'must be 8 to 40 printable characters without spaces', 'could not save the token (storage full?)',
    'pick at least one day', 'must be between 1 and 1440 minutes']) {
    const s = JigLib.tr(m);
    assert.ok(/^[A-Z]/.test(s) && s.endsWith('.'), s);
  }
});
