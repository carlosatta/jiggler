// Pure helpers (no DOM): inlined before core.js in the page and loaded as a module by the node tests.
var JigLib = (function () {
  'use strict';

  var DAYS = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'];
  var DAY_LABELS = { mon: 'Mon', tue: 'Tue', wed: 'Wed', thu: 'Thu', fri: 'Fri', sat: 'Sat', sun: 'Sun' };

  // Server messages are short English fragments ("must be between 5 and 3600"); show them as sentences.
  function tr(message) {
    if (!message) return 'Invalid value.';
    var s = String(message);
    s = s.charAt(0).toUpperCase() + s.slice(1);
    return /[.!?]$/.test(s) ? s : s + '.';
  }

  function pad2(n) { return (n < 10 ? '0' : '') + n; }
  function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }

  function fmtDuration(ms) {
    var s = Math.ceil(ms / 1000), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
    return h > 0 ? h + ':' + pad2(m) + ':' + pad2(sec) : m + ':' + pad2(sec);
  }

  function fmtAgo(ms) {
    var m = Math.floor(ms / 60000);
    if (m < 1) return 'less than a minute ago';
    return plural(m, 'minute', 'minutes') + ' ago';
  }

  // A length of time, as in "for 12 minutes" / "for 1 hour 5 min".
  function fmtSince(ms) {
    var m = Math.floor(ms / 60000);
    if (m < 1) return 'less than a minute';
    if (m < 60) return plural(m, 'minute', 'minutes');
    var h = Math.floor(m / 60), rest = m % 60;
    return plural(h, 'hour', 'hours') + (rest ? ' ' + rest + ' min' : '');
  }

  function hex4(n) { return '0x' + ('0000' + n.toString(16).toUpperCase()).slice(-4); }

  // What the Status page says about Bluetooth, from /api/status: one of four states (off, waiting, discovery,
  // connected). Pure so the wording is testable.
  function connectionView(bt, device) {
    var name = (device && device.bleName) || 'the device';
    var pairing = bt.pairing || {};
    var timed = pairing.open && pairing.remainingMs !== null && pairing.remainingMs !== undefined;
    var state = bt.state || (bt.enabled === false ? 'off' : timed ? 'discovery' : bt.connected ? 'connected' : 'waiting');
    var note = timed ? ' Discovery is open for ' + fmtDuration(pairing.remainingMs) + '.' : '';
    if (state === 'off') {
      return { tone: 'warn', title: 'Bluetooth is off',
        detail: 'The device is invisible and nothing can connect. Turn Bluetooth on and your paired computers reconnect by themselves.', actions: ['enable'] };
    }
    if (bt.connected && bt.computer) {
      var actions = ['disable'];
      if (timed) actions.unshift('closePairing');
      return { tone: 'ok', title: 'Connected to ' + bt.computer.label,
        detail: 'For ' + fmtSince(bt.computer.connectedForMs) + ' · ' + name + '.' + note, actions: actions };
    }
    if (state === 'discovery' || timed) {
      return { tone: 'open', title: 'Discovery is open',
        detail: 'Closes in ' + fmtDuration(pairing.remainingMs) + '. On the computer, choose “' + name + '”.', actions: ['closePairing'] };
    }
    if (!bt.peerCount) {
      return { tone: 'idle', title: 'No computer paired',
        detail: 'Start Discovery, then choose “' + name + '” in the computer’s Bluetooth settings.', actions: ['pair'] };
    }
    return { tone: 'idle', title: 'Waiting for a computer',
      detail: 'Bluetooth is on. A paired computer reconnects by itself when it is on and nearby.', actions: ['pair', 'disable'] };
  }

  function checkNewToken(next, confirm) {
    if (next.length < 8 || next.length > 40 || /\s/.test(next)) return '8 to 40 characters, no spaces.';
    if (next !== confirm) return 'The two tokens do not match.';
    return '';
  }

  // ---- run for / until a time ----------------------------------------------------------------

  // Minutes from `now` to the next occurrence of "HH:MM" local time (1..1440); null if malformed.
  function minutesUntil(hhmm, now) {
    var m = /^(\d{2}):(\d{2})$/.exec(hhmm || '');
    if (!m || +m[1] > 23 || +m[2] > 59) return null;
    var target = new Date(now.getTime());
    target.setHours(+m[1], +m[2], 0, 0);
    if (target.getTime() <= now.getTime()) target.setDate(target.getDate() + 1);  // calendar day, not 24 h: DST-safe
    var diff = target.getTime() - now.getTime();
    return Math.min(1440, Math.max(1, Math.ceil(diff / 60000)));
  }

  // 0 = no limit, a positive number = minutes, null = the choice is not valid yet.
  function runForMinutes(sel, now) {
    if (sel.choice === 'forever') return 0;
    if (sel.choice === 'until') return minutesUntil(sel.time, now);
    if (sel.choice === 'custom') {
      if (!/^\d+$/.test(String(sel.custom || '').trim())) return null;
      var n = parseInt(sel.custom, 10);
      return n >= 1 && n <= 1440 ? n : null;
    }
    var preset = parseInt(sel.choice, 10);
    return preset >= 1 && preset <= 1440 ? preset : null;
  }

  // The wall-clock moment a run that stops in `ms` ends, e.g. "14:40" or "00:30 tomorrow".
  function stopsAt(ms, now) {
    var end = new Date(now.getTime() + ms);
    var label = pad2(end.getHours()) + ':' + pad2(end.getMinutes());
    var sameDay = end.getFullYear() === now.getFullYear() && end.getMonth() === now.getMonth() && end.getDate() === now.getDate();
    return sameDay ? label : label + ' tomorrow';
  }

  // ---- schedules -----------------------------------------------------------------------------

  function describeDays(days) {
    var set = {};
    days.forEach(function (d) { set[d] = true; });
    var idx = DAYS.filter(function (d) { return set[d]; });
    if (idx.length === 0) return 'No days';
    if (idx.length === 7) return 'Every day';
    var runs = [], start = 0;
    for (var i = 1; i <= idx.length; i++) {
      if (i === idx.length || DAYS.indexOf(idx[i]) !== DAYS.indexOf(idx[i - 1]) + 1) {
        runs.push(idx.slice(start, i));
        start = i;
      }
    }
    return runs.map(function (r) {
      if (r.length >= 3) return DAY_LABELS[r[0]] + '–' + DAY_LABELS[r[r.length - 1]];
      return r.map(function (d) { return DAY_LABELS[d]; }).join(', ');
    }).join(', ');
  }

  function scheduleEnd(start, durationMin) {
    var m = /^(\d{2}):(\d{2})$/.exec(start);
    var total = (+m[1]) * 60 + (+m[2]) + durationMin;
    var day = total >= 1440;
    var t = total % 1440;
    return pad2(Math.floor(t / 60)) + ':' + pad2(t % 60) + (day ? ' next day' : '');
  }

  var ACTION_LABELS = {
    start: 'Start the jiggler', stop: 'Stop the jiggler', pause: 'Pause the jiggler', resume: 'Resume the jiggler',
    bluetooth_on: 'Turn Bluetooth on', bluetooth_off: 'Turn Bluetooth off', discovery: 'Open Discovery'
  };

  function toMinutes(hhmm) {
    var m = /^(\d{2}):(\d{2})$/.exec(hhmm || '');
    if (!m || +m[1] > 23 || +m[2] > 59) return null;
    return (+m[1]) * 60 + (+m[2]);
  }

  // "09:00" for 480 minutes -> "17:00" (wraps past midnight).
  function endFromDuration(start, durationMin) {
    var t = (toMinutes(start) + durationMin) % 1440;
    return pad2(Math.floor(t / 60)) + ':' + pad2(t % 60);
  }

  // The length between two times of day, 1..1440; an end at or before the start means "the next day", and the
  // same time means a full 24 hours. null when either time is not valid.
  function durationFromEnd(start, end) {
    var a = toMinutes(start), b = toMinutes(end);
    if (a === null || b === null) return null;
    return ((b - a + 1440) % 1440) || 1440;
  }

  function describeRepeat(s) {
    if (!s.times) return 'Repeats every week';
    var runs = function (n) { return n + (n === 1 ? ' run' : ' runs'); };
    if (!s.enabled && s.remaining === 0) return 'Finished (' + runs(s.times) + ' done)';
    return s.remaining === s.times ? runs(s.times) : s.remaining + ' of ' + runs(s.times) + ' left';
  }

  function describeJitter(min) { return min ? '± ' + min + ' min' : ''; }

  function scheduleSummary(s) {
    var line = s.kind === 'action'
      ? describeDays(s.days) + ' at ' + s.start + ': ' + (ACTION_LABELS[s.action] || s.action)
      : describeDays(s.days) + ', ' + s.start + ' to ' + scheduleEnd(s.start, s.durationMin);
    if (s.jitterMin) line += ' · ' + describeJitter(s.jitterMin);
    return s.times ? line + ' · ' + describeRepeat(s) : line;
  }

  // One line about the device clock, from the `time` object of /api/status or /api/time.
  function clockLine(time) {
    if (!time || !time.synced) return 'Not set: schedules wait for the time (needs internet, or set it by hand).';
    var day = time.weekday ? time.weekday.charAt(0).toUpperCase() + time.weekday.slice(1) + ' ' : '';
    var source = time.source === 'manual' ? 'set by hand' : 'from NTP';
    return day + time.localTime + ' · ' + time.timezone + ' · ' + source;
  }

  // What the Firmware card says, from GET /api/updates. Pure so the wording is testable.
  function describeUpdate(u) {
    var latest = u.latest && u.latest.version;
    if (u.state === 'checking') return { text: 'Looking for a new version…', tone: 'busy', canInstall: false, working: true };
    if (u.state === 'installing') return { text: 'Downloading ' + (latest || 'the new version') + ': ' + (u.progress || 0) + '%. Do not unplug the device.', tone: 'busy', canInstall: false, working: true };
    if (u.state === 'restarting') return { text: 'Installed. The device is restarting…', tone: 'good', canInstall: false, working: true };
    var checked = u.checkedAgoMs === null || u.checkedAgoMs === undefined ? null : fmtAgo(u.checkedAgoMs);
    var lead;
    if (u.available && u.blocked) lead = 'Version ' + latest + ' is available, but the last attempt to install it did not take effect. You can try again, or install it from a file.';
    else if (u.available) lead = 'Version ' + latest + ' is available.';
    else if (checked !== null && latest) lead = 'You are up to date (' + u.current + '), checked ' + checked + '.';
    else if (checked !== null) lead = 'Checked ' + checked + '.';
    else lead = u.mode === 'ignore' ? 'Updates are ignored: the device does not look for them by itself.' : 'Not checked yet. The device looks by itself about every day.';
    if (u.error) lead += ' ' + tr(u.error);
    return { text: lead, tone: u.error ? 'bad' : u.available ? 'ok' : 'idle', canInstall: !!u.available, working: false };
  }

  // The address people see for a repository: "github.com/owner/repo".
  function repoLabel(url) { return String(url || '').replace(/^https?:\/\//, ''); }

  return { DAYS: DAYS, DAY_LABELS: DAY_LABELS, tr: tr, fmtDuration: fmtDuration, fmtAgo: fmtAgo, fmtSince: fmtSince, hex4: hex4,
    connectionView: connectionView, checkNewToken: checkNewToken, minutesUntil: minutesUntil, runForMinutes: runForMinutes,
    stopsAt: stopsAt, describeDays: describeDays, scheduleEnd: scheduleEnd, scheduleSummary: scheduleSummary,
    ACTION_LABELS: ACTION_LABELS, describeJitter: describeJitter, clockLine: clockLine, describeUpdate: describeUpdate, repoLabel: repoLabel,
    endFromDuration: endFromDuration, durationFromEnd: durationFromEnd, describeRepeat: describeRepeat };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = JigLib;
