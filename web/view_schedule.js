// Schedule: weekly windows that start and stop the jiggler, and the timezone they are read in.
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr;
  var labeled = App.labeled, clearErrors = App.clearErrors, showError = App.showError;

  // ---- Schedules ----
  var sched = { timezone: 'Europe/Rome', schedules: [] };
  var MAX_SCHEDULES = 8;

  function readSchedules() {
    var out = [];
    $('schedList').querySelectorAll('.sched').forEach(function (card) {
      var days = [];
      card.querySelectorAll('.days button').forEach(function (b) { if (b.getAttribute('aria-pressed') === 'true') days.push(b.getAttribute('data-day')); });
      out.push({
        id: card.querySelector('[data-k=id]').value.trim(),
        enabled: card.querySelector('[data-k=enabled]').checked,
        days: days,
        start: card.querySelector('[data-k=start]').value,
        durationMin: Number(card.querySelector('[data-k=durationMin]').value)
      });
    });
    return out;
  }

  function refreshSummary(card) {
    var start = card.querySelector('[data-k=start]').value;
    var dur = Number(card.querySelector('[data-k=durationMin]').value);
    var days = [];
    card.querySelectorAll('.days button').forEach(function (b) { if (b.getAttribute('aria-pressed') === 'true') days.push(b.getAttribute('data-day')); });
    var sum = card.querySelector('.sched-sum');
    var ok = /^\d{2}:\d{2}$/.test(start) && dur >= 1 && dur <= 1440 && Number.isInteger(dur);
    sum.textContent = ok ? JigLib.scheduleSummary({ days: days, start: start, durationMin: dur }) : JigLib.describeDays(days) + ', set a start time and a duration';
    sum.className = 'sched-sum' + (card.querySelector('[data-k=enabled]').checked ? '' : ' off');
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
      card.appendChild(labeled('Name', id, 'schedules[' + i + '].id'));

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
      var dur = el('input', { inputmode: 'numeric', autocomplete: 'off' });
      dur.setAttribute('data-k', 'durationMin');
      dur.value = String(sc.durationMin);
      dur.addEventListener('input', function () { refreshSummary(card); });
      card.appendChild(el('div', { class: 'row3' }, [labeled('Starts at', start, 'schedules[' + i + '].start'), labeled('Lasts (minutes)', dur, 'schedules[' + i + '].durationMin')]));

      var rm = el('button', { class: 'danger', type: 'button', text: 'Remove' });
      rm.onclick = function () { sched.schedules = readSchedules(); sched.schedules.splice(i, 1); renderSchedules(); };
      card.appendChild(el('div', { class: 'actions' }, [rm]));
      box.appendChild(card);
      refreshSummary(card);
    });
    $('schedEmpty').hidden = sched.schedules.length > 0;
    $('schedAdd').disabled = sched.schedules.length >= MAX_SCHEDULES;
  }

  function fillTimezones(list) {
    var sel = $('tzSelect');
    if (sel.options.length === list.length) return;
    sel.textContent = '';
    list.forEach(function (z) { sel.appendChild(el('option', { value: z, text: z.replace('_', ' ') })); });
  }

  function loadSchedules() {
    App.call('GET', '/api/schedules').then(function (res) {
      if (res.status !== 200) return;
      sched = { timezone: res.data.timezone, schedules: res.data.schedules };
      fillTimezones(res.data.timezones);
      $('tzSelect').value = sched.timezone;
      renderSchedules();
    }).catch(function () {});
  }

  $('schedAdd').onclick = function () {
    sched.schedules = readSchedules();
    sched.timezone = $('tzSelect').value;
    var n = 1;
    while (sched.schedules.some(function (x) { return x.id === 'schedule-' + n; })) n++;
    sched.schedules.push({ id: 'schedule-' + n, enabled: true, days: ['mon', 'tue', 'wed', 'thu', 'fri'], start: '09:00', durationMin: 480 });
    renderSchedules();
  };

  $('schedSave').onclick = function () {
    var root = $('view-schedule');
    clearErrors($('schedList'));
    root.querySelector('[data-err="timezone"]').textContent = '';
    say('', '', 'schedMsg');
    App.call('PUT', '/api/schedules', { timezone: $('tzSelect').value, schedules: readSchedules() }).then(function (res) {
      if (res.status === 200) {
        sched = { timezone: res.data.timezone, schedules: res.data.schedules };
        renderSchedules();
        say('Schedules saved. Saving never starts or stops the jiggler by itself; the next window edge does.', 'good', 'schedMsg');
        App.poll();
        return;
      }
      var d = res.data;
      if (!d.field || !showError(root, d.field, d.message)) say(tr(d.message) || 'Could not save.', 'bad', 'schedMsg');
      else say('Check the highlighted fields.', 'bad', 'schedMsg');
    }).catch(function () {});
  };

  function renderClock(s) {
    var sc = s.schedule;
    var note = $('clockNote');
    if (!sc) { note.textContent = ''; return; }
    if (!sc.timeSynced) note.textContent = 'The device clock is not set yet: schedules start working once it has internet access.';
    else note.textContent = 'Device clock: ' + sc.localTime + (sc.active ? ' · a schedule window is open now.' : '.');
  }

  App.view('schedule', {
    load: function () {
      if (App.status()) renderClock(App.status());
      loadSchedules();
    },
    onStatus: function (s) { renderClock(s); }
  });
})();
