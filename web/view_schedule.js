// Schedule: weekly windows that start and stop the jiggler, and actions done at one time of day.
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr;
  var labeled = App.labeled, clearErrors = App.clearErrors, showError = App.showError;

  var sched = { schedules: [] };
  var MAX_SCHEDULES = 8;
  var ACTIONS = ['start', 'stop', 'pause', 'resume', 'bluetooth_on', 'bluetooth_off', 'discovery'];

  function readCard(card) {
    var days = [];
    card.querySelectorAll('.days button').forEach(function (b) { if (b.getAttribute('aria-pressed') === 'true') days.push(b.getAttribute('data-day')); });
    var kind = card.querySelector('[data-k=kind]').value;
    var start = card.querySelector('[data-k=start]').value;
    var end = card.querySelector('[data-k=end]').value;
    var mode = card.querySelector('[data-k=repeat]').value;
    var timesInput = card.querySelector('[data-k=times]');
    var times = mode === 'times' ? Number(timesInput.value) : null;
    var remaining = timesInput.getAttribute('data-remaining');
    var jitter = card.querySelector('[data-k=jitter]').value.trim();
    var out = {
      id: card.querySelector('[data-k=id]').value.trim(),
      enabled: card.querySelector('[data-k=enabled]').checked,
      kind: kind,
      days: days,
      start: start,
      jitterMin: /^\d+$/.test(jitter) ? Number(jitter) : 0,
      times: times,
      // progress is kept while the count is unchanged; a new count starts a fresh run
      remaining: times && remaining !== null && Number(remaining) <= times && Number(timesInput.getAttribute('data-times')) === times ? Number(remaining) : times
    };
    if (kind === 'action') out.action = card.querySelector('[data-k=action]').value;
    else out.durationMin = JigLib.durationFromEnd(start, end);
    return out;
  }

  function readSchedules() {
    var out = [];
    $('schedList').querySelectorAll('.sched').forEach(function (card) { out.push(readCard(card)); });
    return out;
  }

  function refreshSummary(card) {
    var s = readCard(card);
    var isAction = s.kind === 'action';
    var sum = card.querySelector('.sched-sum');
    var ok = /^\d{2}:\d{2}$/.test(s.start) && (isAction || s.durationMin !== null);
    sum.textContent = ok ? JigLib.scheduleSummary(s) : JigLib.describeDays(s.days) + ', set ' + (isAction ? 'a time' : 'a start and an end time');
    sum.className = 'sched-sum' + (s.enabled ? '' : ' off');
    card.querySelector('[data-k=times]').closest('.field').hidden = card.querySelector('[data-k=repeat]').value !== 'times';
    card.querySelector('[data-k=action]').closest('.field').hidden = !isAction;
    card.querySelector('[data-k=end]').closest('.field').hidden = isAction;
    card.querySelector('.when-hint').hidden = isAction;
    card.querySelector('[data-k=start]').closest('.field').querySelector('label').textContent = isAction ? 'At' : 'Starts at';
  }

  function renderSchedules() {
    var box = $('schedList');
    box.textContent = '';
    sched.schedules.forEach(function (sc, i) {
      var card = el('div', { class: 'sched' });
      var sum = el('span', { class: 'sched-sum' });
      var en = el('input', { type: 'checkbox' });
      en.setAttribute('data-k', 'enabled');
      en.checked = sc.enabled;
      en.addEventListener('change', function () { refreshSummary(card); });
      card.appendChild(el('div', { class: 'sched-head' }, [sum, el('label', { class: 'inline' }, [en, el('span', { text: 'On' })])]));

      var id = el('input', { autocomplete: 'off', placeholder: 'a-z 0-9 _ -' });
      id.setAttribute('data-k', 'id');
      id.value = sc.id;
      var kind = el('select');
      kind.setAttribute('data-k', 'kind');
      kind.appendChild(el('option', { value: 'window', text: 'Window' }));
      kind.appendChild(el('option', { value: 'action', text: 'Action' }));
      kind.value = sc.kind === 'action' ? 'action' : 'window';
      kind.addEventListener('change', function () { refreshSummary(card); });
      card.appendChild(el('div', { class: 'row3' }, [labeled('Name', id, 'schedules[' + i + '].id'), labeled('Type', kind, 'schedules[' + i + '].kind')]));

      var action = el('select');
      action.setAttribute('data-k', 'action');
      ACTIONS.forEach(function (a) { action.appendChild(el('option', { value: a, text: JigLib.ACTION_LABELS[a] })); });
      action.value = sc.action || 'start';
      action.addEventListener('change', function () { refreshSummary(card); });
      card.appendChild(labeled('Do', action, 'schedules[' + i + '].action'));

      var days = el('div', { class: 'days', role: 'group', 'aria-label': 'Days' });
      JigLib.DAYS.forEach(function (d) {
        var b = el('button', { type: 'button', text: JigLib.DAY_LABELS[d] });
        b.setAttribute('data-day', d);
        b.setAttribute('aria-pressed', sc.days.indexOf(d) >= 0 ? 'true' : 'false');
        b.addEventListener('click', function () {
          b.setAttribute('aria-pressed', b.getAttribute('aria-pressed') === 'true' ? 'false' : 'true');
          refreshSummary(card);
        });
        days.appendChild(b);
      });
      card.appendChild(days);
      var dayErr = el('div', { class: 'err' });
      dayErr.setAttribute('data-err', 'schedules[' + i + '].days');
      card.appendChild(dayErr);

      var start = el('input', { type: 'time' });
      start.setAttribute('data-k', 'start');
      start.value = sc.start;
      start.addEventListener('input', function () { refreshSummary(card); });
      var end = el('input', { type: 'time' });
      end.setAttribute('data-k', 'end');
      end.value = sc.durationMin ? JigLib.endFromDuration(sc.start, sc.durationMin) : '17:00';
      end.addEventListener('input', function () { refreshSummary(card); });
      card.appendChild(el('div', { class: 'row3' }, [labeled('Starts at', start, 'schedules[' + i + '].start'), labeled('Ends at', end, 'schedules[' + i + '].durationMin')]));
      card.appendChild(el('p', { class: 'hint when-hint', text: 'An end time earlier than the start means the next day. The same time means 24 hours.' }));

      var jitter = el('input', { inputmode: 'numeric', autocomplete: 'off', placeholder: '0' });
      jitter.setAttribute('data-k', 'jitter');
      jitter.value = sc.jitterMin ? String(sc.jitterMin) : '';
      jitter.addEventListener('input', function () { refreshSummary(card); });
      card.appendChild(labeled('Random shift (± minutes, 0 to 120)', jitter, 'schedules[' + i + '].jitterMin'));
      card.appendChild(el('p', { class: 'hint', text: 'Each time moves by a random amount up to this many minutes earlier or later, chosen afresh every day (a window’s start and end move independently).' }));

      var repeat = el('select');
      repeat.setAttribute('data-k', 'repeat');
      repeat.appendChild(el('option', { value: 'forever', text: 'Forever' }));
      repeat.appendChild(el('option', { value: 'times', text: 'Limited' }));
      repeat.value = sc.times ? 'times' : 'forever';
      repeat.addEventListener('change', function () { refreshSummary(card); });
      var times = el('input', { inputmode: 'numeric', autocomplete: 'off' });
      times.setAttribute('data-k', 'times');
      times.value = sc.times ? String(sc.times) : '1';
      if (sc.times) { times.setAttribute('data-remaining', String(sc.remaining)); times.setAttribute('data-times', String(sc.times)); }
      times.addEventListener('input', function () { refreshSummary(card); });
      card.appendChild(el('div', { class: 'row3' }, [labeled('Repeat every week', repeat, 'schedules[' + i + '].repeat'), labeled('Number of runs', times, 'schedules[' + i + '].times')]));

      var rm = el('button', { class: 'danger', type: 'button', text: 'Remove' });
      rm.onclick = function () { sched.schedules = readSchedules(); sched.schedules.splice(i, 1); renderSchedules(); };
      card.appendChild(el('div', { class: 'actions' }, [rm]));
      box.appendChild(card);
      refreshSummary(card);
    });
    $('schedEmpty').hidden = sched.schedules.length > 0;
    $('schedAdd').disabled = $('schedAddAction').disabled = sched.schedules.length >= MAX_SCHEDULES;
  }

  function loadSchedules() {
    App.call('GET', '/api/schedules').then(function (res) {
      if (res.status !== 200) return;
      sched = { schedules: res.data.schedules };
      renderSchedules();
    }).catch(function () {});
  }

  function add(kind) {
    sched.schedules = readSchedules();
    var n = 1, prefix = kind === 'action' ? 'action-' : 'window-';
    while (sched.schedules.some(function (x) { return x.id === prefix + n; })) n++;
    var base = { id: prefix + n, enabled: true, kind: kind, days: ['mon', 'tue', 'wed', 'thu', 'fri'], start: '09:00', jitterMin: 0, times: null, remaining: null };
    if (kind === 'action') base.action = 'start'; else base.durationMin = 480;
    sched.schedules.push(base);
    renderSchedules();
  }
  $('schedAdd').onclick = function () { add('window'); };
  $('schedAddAction').onclick = function () { add('action'); };

  $('schedSave').onclick = function () {
    var root = $('view-schedule');
    clearErrors($('schedList'));
    say('', '', 'schedMsg');
    App.call('PUT', '/api/schedules', { schedules: readSchedules() }).then(function (res) {
      if (res.status === 200) {
        sched = { schedules: res.data.schedules };
        renderSchedules();
        say('Schedules saved. A window that is open right now starts the jiggler.', 'good', 'schedMsg');
        App.poll();
        return;
      }
      var d = res.data;
      if (!d.field || !showError(root, d.field, d.message)) say(tr(d.message) || 'Could not save.', 'bad', 'schedMsg');
      else say('Check the highlighted fields.', 'bad', 'schedMsg');
    }).catch(function () {});
  };

  function renderClock(s) {
    var note = $('clockNote');
    if (!s.time) { note.textContent = ''; return; }
    note.textContent = 'Device clock: ' + JigLib.clockLine(s.time) + (s.schedule && s.schedule.active ? ' · a window is open now.' : '.') + ' Change the timezone or NTP server in System → Time.';
  }

  App.view('schedule', {
    load: function () {
      if (App.status()) renderClock(App.status());
      loadSchedules();
    },
    onStatus: function (s) { renderClock(s); }
  });
})();
