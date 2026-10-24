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

  // What the Status page says about the Bluetooth link, from /api/status. Pure so the wording is testable.
  function connectionView(bt, device) {
    var name = (device && device.bleName) || 'the device';
    var pairing = bt.pairing || {};
    var timed = pairing.open && pairing.remainingMs !== null && pairing.remainingMs !== undefined;
    var windowNote = timed ? ' Pairing is open for ' + fmtDuration(pairing.remainingMs) + '.' : '';
    if (bt.connected && bt.computer) {
      var actions = ['disconnect'];
      if (timed) actions.push('closePairing');
      return { tone: 'ok', title: 'Connected to ' + bt.computer.label,
        detail: 'For ' + fmtSince(bt.computer.connectedForMs) + ' · ' + name + '.' + windowNote, actions: actions };
    }
    if (bt.held) {
      var left = bt.heldRemainingMs !== null && bt.heldRemainingMs !== undefined ? fmtDuration(bt.heldRemainingMs) : '';
      return { tone: 'warn', title: 'Disconnected',
        detail: (left ? 'Hidden from computers for another ' + left + '; after that your computer can reconnect.' : 'Hidden from computers.') +
          ' Press Reconnect to show the device now.', actions: ['reconnect'] };
    }
    if (timed) {
      return { tone: 'open', title: 'Pairing is open',
        detail: 'Closes in ' + fmtDuration(pairing.remainingMs) + '. On the computer, choose “' + name + '”.', actions: ['closePairing'] };
    }
    if (!bt.peerCount) {
      return { tone: 'idle', title: 'No computer paired',
        detail: 'Open pairing, then choose “' + name + '” in the computer’s Bluetooth settings.', actions: ['pair'] };
    }
    return { tone: 'idle', title: 'No computer connected',
      detail: 'It reconnects by itself when a paired computer is on and nearby.', actions: ['pair'] };
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

  function scheduleSummary(s) {
    var line = describeDays(s.days) + ', ' + s.start + ' to ' + scheduleEnd(s.start, s.durationMin);
    return s.times ? line + ' · ' + describeRepeat(s) : line;
  }

  return { DAYS: DAYS, DAY_LABELS: DAY_LABELS, tr: tr, fmtDuration: fmtDuration, fmtAgo: fmtAgo, fmtSince: fmtSince, hex4: hex4,
    connectionView: connectionView, checkNewToken: checkNewToken, minutesUntil: minutesUntil, runForMinutes: runForMinutes,
    stopsAt: stopsAt, describeDays: describeDays, scheduleEnd: scheduleEnd, scheduleSummary: scheduleSummary,
    endFromDuration: endFromDuration, durationFromEnd: durationFromEnd, describeRepeat: describeRepeat };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = JigLib;
