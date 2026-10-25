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
  state: 'waiting', enabled: true, connected: false, peerCount: 1, computer: null,
  pairing: { open: false, remainingMs: null }
}, over);
const device = { bleName: 'MX Master 3S' };

test('connection view: connected names the computer and offers to turn Bluetooth off', () => {
  const v = JigLib.connectionView(bt({ state: 'connected', connected: true, computer: { label: 'Computer 2', connectedForMs: 12 * 60000 } }), device);
  assert.equal(v.tone, 'ok');
  assert.equal(v.title, 'Connected to Computer 2');
  assert.match(v.detail, /For 12 minutes/);
  assert.deepEqual(v.actions, ['disable']);
});

test('connection view: off says nothing can connect and offers to turn it on', () => {
  const v = JigLib.connectionView(bt({ state: 'off', enabled: false }), device);
  assert.equal(v.tone, 'warn');
  assert.equal(v.title, 'Bluetooth is off');
  assert.match(v.detail, /invisible/);
  assert.deepEqual(v.actions, ['enable']);
});

test('connection view: Discovery shows the countdown and can be stopped', () => {
  const v = JigLib.connectionView(bt({ state: 'discovery', pairing: { open: true, remainingMs: 92000 } }), device);
  assert.equal(v.title, 'Discovery is open');
  assert.match(v.detail, /1:32/);
  assert.deepEqual(v.actions, ['closePairing']);
});

test('connection view: waiting with paired computers offers Discovery or turning Bluetooth off', () => {
  const v = JigLib.connectionView(bt(), device);
  assert.equal(v.tone, 'idle');
  assert.equal(v.title, 'Waiting for a computer');
  assert.deepEqual(v.actions, ['pair', 'disable']);
});

test('connection view: connected while Discovery is open can stop Discovery or turn Bluetooth off', () => {
  const v = JigLib.connectionView(
    bt({ state: 'discovery', connected: true, computer: { label: 'Computer 1', connectedForMs: 1000 }, pairing: { open: true, remainingMs: 30000 } }), device);
  assert.deepEqual(v.actions, ['closePairing', 'disable']);
  assert.match(v.detail, /Discovery is open/);
});

test('connection view: nothing paired says so and offers Discovery', () => {
  const v = JigLib.connectionView(bt({ peerCount: 0 }), device);
  assert.equal(v.tone, 'idle');
  assert.equal(v.title, 'No computer paired');
  assert.match(v.detail, /Start Discovery/);
  assert.deepEqual(v.actions, ['pair']);
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

test('endFromDuration and durationFromEnd convert between a length and an end time', () => {
  assert.equal(JigLib.endFromDuration('09:00', 480), '17:00');
  assert.equal(JigLib.endFromDuration('22:00', 480), '06:00');
  assert.equal(JigLib.endFromDuration('09:00', 1440), '09:00');
  assert.equal(JigLib.durationFromEnd('09:00', '17:00'), 480);
  assert.equal(JigLib.durationFromEnd('22:00', '06:00'), 480);   // ends the next day
  assert.equal(JigLib.durationFromEnd('09:00', '09:00'), 1440);  // the same time means a full day
  assert.equal(JigLib.durationFromEnd('09:00', '09:01'), 1);
  assert.equal(JigLib.durationFromEnd('', '09:00'), null);
  assert.equal(JigLib.durationFromEnd('09:00', 'x'), null);
});

test('describeRepeat says how many runs a schedule has left', () => {
  assert.equal(JigLib.describeRepeat({ enabled: true, times: null, remaining: null }), 'Repeats every week');
  assert.equal(JigLib.describeRepeat({ enabled: true, times: 3, remaining: 3 }), '3 runs');
  assert.equal(JigLib.describeRepeat({ enabled: true, times: 3, remaining: 1 }), '1 of 3 runs left');
  assert.equal(JigLib.describeRepeat({ enabled: true, times: 1, remaining: 1 }), '1 run');
  assert.equal(JigLib.describeRepeat({ enabled: false, times: 3, remaining: 0 }), 'Finished (3 runs done)');
  assert.equal(JigLib.describeRepeat({ enabled: false, times: 3, remaining: 2 }), '2 of 3 runs left');
});

test('scheduleSummary includes the repeat information', () => {
  assert.equal(JigLib.scheduleSummary({ days: ['mon'], start: '09:00', durationMin: 60, enabled: true, times: 2, remaining: 1 }),
    'Mon, 09:00 to 10:00 · 1 of 2 runs left');
  assert.equal(JigLib.scheduleSummary({ days: ['mon'], start: '09:00', durationMin: 60, enabled: true, times: null }),
    'Mon, 09:00 to 10:00');
});

test('scheduleSummary describes actions and the random shift', () => {
  assert.equal(JigLib.scheduleSummary({ kind: 'action', action: 'pause', days: ['mon', 'tue', 'wed', 'thu', 'fri'], start: '12:30' }),
    'Mon–Fri at 12:30: Pause the jiggler');
  assert.equal(JigLib.scheduleSummary({ kind: 'action', action: 'bluetooth_off', days: ['sat', 'sun'], start: '22:00', jitterMin: 10 }),
    'Sat, Sun at 22:00: Turn Bluetooth off · ± 10 min');
  assert.equal(JigLib.scheduleSummary({ kind: 'window', days: ['mon'], start: '09:00', durationMin: 480, jitterMin: 15 }),
    'Mon, 09:00 to 17:00 · ± 15 min');
  assert.equal(JigLib.describeJitter(0), '');
  for (const a of ['start', 'stop', 'pause', 'resume', 'bluetooth_on', 'bluetooth_off', 'discovery']) assert.ok(JigLib.ACTION_LABELS[a], a);
});

test('clockLine says where the time comes from, or that it is not set', () => {
  assert.equal(JigLib.clockLine({ synced: true, localTime: '09:05', weekday: 'wed', timezone: 'Europe/Rome', source: 'ntp' }), 'Wed 09:05 · Europe/Rome · from NTP');
  assert.equal(JigLib.clockLine({ synced: true, localTime: '09:05', weekday: 'wed', timezone: 'UTC', source: 'manual' }), 'Wed 09:05 · UTC · set by hand');
  assert.match(JigLib.clockLine({ synced: false }), /Not set/);
  assert.match(JigLib.clockLine(undefined), /Not set/);
});
