// Movement: weekly schedules, random-movement configuration, scripts and automations.
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr;
  var labeled = App.labeled, clearErrors = App.clearErrors, showError = App.showError;
  var FIELDS = [
    { id: 'minIntervalSec', scale: 1 }, { id: 'maxIntervalSec', scale: 1 }, { id: 'amplitudePx', scale: 1 },
    { id: 'clickProbability', scale: 100 }, { id: 'scrollProbability', scale: 100 }
  ];
  var dirty = {}, config = null;

  function fieldError(id, text) { var e = $('e_' + id); if (e) e.textContent = text; }

  function populate(s) {
    config = s.config;
    FIELDS.forEach(function (f) {
      if (dirty[f.id] || document.activeElement === $(f.id)) return;
      $(f.id).value = String(Math.round(s.config[f.id] * f.scale));
    });
  }
  FIELDS.forEach(function (f) { $(f.id).addEventListener('input', function () { dirty[f.id] = true; }); });

  $('cfg').addEventListener('submit', function (ev) {
    ev.preventDefault();
    FIELDS.forEach(function (f) { fieldError(f.id, ''); $(f.id).removeAttribute('aria-invalid'); });
    if (!config) return;
    var body = {}, invalid = false;
    FIELDS.forEach(function (f) {
      var raw = $(f.id).value.trim().replace(',', '.');
      if (raw === '') { fieldError(f.id, 'This field is required.'); invalid = true; return; }
      var n = Number(raw);
      if (!isFinite(n)) { fieldError(f.id, 'Enter a number.'); invalid = true; return; }
      if (n !== Math.round(config[f.id] * f.scale)) body[f.id] = n / f.scale;
    });
    if (invalid) return;
    say('', '', 'cfgMsg');
    if (Object.keys(body).length === 0) { say('Nothing to save.', '', 'cfgMsg'); return; }
    App.call('PUT', '/api/config', body).then(function (res) {
      if (res.status === 200) { dirty = {}; say('Configuration saved.', 'good', 'cfgMsg'); App.poll(); return; }
      var d = res.data;
      if (d.field && $('e_' + d.field)) { fieldError(d.field, tr(d.message)); $(d.field).setAttribute('aria-invalid', 'true'); }
      else say('Could not save.', 'bad', 'cfgMsg');
    }).catch(function () {});
  });

  // ---- Script ----
  var scriptList = [], builtin = false, isNew = false;
  var TEMPLATE = '{\n  "steps": [\n    { "op": "key", "key": "F15" },\n    { "op": "wait", "minMs": 500, "maxMs": 1500 }\n  ]\n}';

  function setScriptButtons() {
    $('scrSave').disabled = builtin;
    $('scrDelete').disabled = builtin || isNew;
    $('scrRun').disabled = isNew;
    $('scrName').disabled = !isNew;
  }
  function renderScriptSelect(selected) {
    var sel = $('scrSelect');
    sel.textContent = '';
    scriptList.forEach(function (s) { sel.appendChild(el('option', { value: s.name, text: s.name + (s.builtin ? ' (built in)' : '') })); });
    if (selected) sel.value = selected;
  }
  function openScript(name) {
    App.call('GET', '/api/scripts/' + encodeURIComponent(name)).then(function (res) {
      if (res.status !== 200) { say('Script not found.', 'bad', 'scrMsg'); return; }
      isNew = false;
      builtin = res.data.builtin;
      $('scrName').value = res.data.name;
      $('scrBody').value = JSON.stringify(res.data.script, null, 2);
      clearErrors($('scriptsBox'));
      setScriptButtons();
    }).catch(function () {});
  }
  function loadScripts(selected) {
    return App.call('GET', '/api/scripts').then(function (res) {
      scriptList = res.data.scripts;
      renderScriptSelect(selected);
      if (scriptList.length && !isNew) openScript($('scrSelect').value);
      else setScriptButtons();
    }).catch(function () {});
  }
  function scriptError(res) {
    var d = res.data;
    clearErrors($('scriptsBox'));
    if (d.field === 'name') { $('e_scrName').textContent = tr(d.message); return; }
    $('e_scrBody').textContent = (d.field ? d.field + ': ' : '') + tr(d.message);
  }
  $('scrSelect').addEventListener('change', function () { isNew = false; openScript($('scrSelect').value); });
  $('scrNew').onclick = function () {
    isNew = true; builtin = false;
    $('scrName').value = '';
    $('scrBody').value = TEMPLATE;
    clearErrors($('scriptsBox'));
    say('', '', 'scrMsg');
    setScriptButtons();
    $('scrName').focus();
  };
  $('scrSave').onclick = function () {
    var name = $('scrName').value.trim();
    say('', '', 'scrMsg');
    clearErrors($('scriptsBox'));
    if (!name) { $('e_scrName').textContent = 'Enter a name.'; return; }
    App.call('PUT', '/api/scripts/' + encodeURIComponent(name), $('scrBody').value).then(function (res) {
      if (res.status === 200) { isNew = false; say('Script saved.', 'good', 'scrMsg'); loadScripts(name); }
      else scriptError(res);
    }).catch(function () {});
  };
  $('scrRun').onclick = function () {
    App.call('POST', '/api/scripts/' + encodeURIComponent($('scrName').value.trim()) + '/run').then(function (res) {
      if (res.status === 202) say('Script started.', 'good', 'scrMsg');
      else say(res.data.error === 'mouse_not_connected' ? 'No computer connected.' : 'Could not run it.', 'bad', 'scrMsg');
    }).catch(function () {});
  };
  App.confirmInline($('scrDelete'), 'Confirm: delete', function () {
    App.call('DELETE', '/api/scripts/' + encodeURIComponent($('scrName').value.trim())).then(function (res) {
      if (res.status === 200) { isNew = false; say('Script deleted.', 'good', 'scrMsg'); loadScripts(); }
      else say(tr(res.data.message) || 'Could not delete.', 'bad', 'scrMsg');
    }).catch(function () {});
  });

  // ---- Automazioni ----
  var autos = [];
  function renderAutos() {
    var box = $('autoList');
    box.textContent = '';
    autos.forEach(function (a, i) {
      var row = el('div', { class: 'dev' });
      var id = el('input', { autocomplete: 'off' });
      id.setAttribute('data-k', 'id');
      id.value = a.id;
      row.appendChild(labeled('Name', id, 'automations[' + i + '].id'));
      var sel = el('select');
      sel.setAttribute('data-k', 'script');
      scriptList.forEach(function (s) { sel.appendChild(el('option', { value: s.name, text: s.name })); });
      if (!scriptList.some(function (s) { return s.name === a.script; })) sel.appendChild(el('option', { value: a.script, text: a.script + ' (missing)' }));
      sel.value = a.script;
      row.appendChild(labeled('Script', sel, 'automations[' + i + '].script'));
      var pair = el('div', { class: 'row' });
      [['minIntervalSec', 'Minimum (s)'], ['maxIntervalSec', 'Maximum (s)']].forEach(function (f) {
        var inp = el('input', { inputmode: 'numeric', autocomplete: 'off' });
        inp.setAttribute('data-k', f[0]);
        inp.value = String(a[f[0]]);
        pair.appendChild(labeled(f[1], inp, 'automations[' + i + '].' + f[0]));
      });
      row.appendChild(pair);
      var en = el('input', { type: 'checkbox' });
      en.setAttribute('data-k', 'enabled');
      en.checked = a.enabled;
      row.appendChild(el('div', { class: 'field' }, [el('label', { class: 'inline' }, [en, el('span', { text: 'Enabled' })])]));
      var rm = el('button', { class: 'danger', text: 'Remove' });
      rm.onclick = function () { collectAutos(); autos.splice(i, 1); renderAutos(); };
      row.appendChild(rm);
      box.appendChild(row);
    });
    $('autoAdd').disabled = autos.length >= 4;
  }
  function collectAutos() {
    var out = [];
    $('autoList').querySelectorAll('.dev').forEach(function (row) {
      var g = function (k) { return row.querySelector('[data-k=' + k + ']'); };
      out.push({ id: g('id').value.trim(), script: g('script').value, minIntervalSec: Number(g('minIntervalSec').value),
        maxIntervalSec: Number(g('maxIntervalSec').value), enabled: g('enabled').checked });
    });
    autos = out;
    return out;
  }
  function loadAutos() {
    Promise.all([App.call('GET', '/api/scripts'), App.call('GET', '/api/automations')]).then(function (r) {
      scriptList = r[0].data.scripts;
      autos = r[1].data.automations;
      renderAutos();
    }).catch(function () {});
  }
  $('autoAdd').onclick = function () {
    collectAutos();
    var n = 1;
    while (autos.some(function (a) { return a.id === 'auto-' + n; })) n++;
    autos.push({ id: 'auto-' + n, script: scriptList.length ? scriptList[0].name : 'keepalive-key', minIntervalSec: 60, maxIntervalSec: 180, enabled: false });
    renderAutos();
  };
  $('autoSave').onclick = function () {
    var root = $('autoList');
    clearErrors(root);
    say('', '', 'autoMsg');
    App.call('PUT', '/api/automations', { automations: collectAutos() }).then(function (res) {
      if (res.status === 200) { autos = res.data.automations; renderAutos(); say('Automations saved.', 'good', 'autoMsg'); return; }
      if (!res.data.field || !showError(root, res.data.field, res.data.message)) say(tr(res.data.message) || 'Could not save.', 'bad', 'autoMsg');
      else say('Check the highlighted fields.', 'bad', 'autoMsg');
    }).catch(function () {});
  };

  $('scriptsBox').addEventListener('toggle', function () { if ($('scriptsBox').open) loadScripts(); });
  $('autoBox').addEventListener('toggle', function () { if ($('autoBox').open) loadAutos(); });

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
    var root = $('view-movement');
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

  App.view('movement', {
    load: function () {
      if (App.status()) { populate(App.status()); renderClock(App.status()); }
      loadSchedules();
      if ($('scriptsBox').open) loadScripts();
      if ($('autoBox').open) loadAutos();
    },
    onStatus: function (s) { populate(s); renderClock(s); }
  });
})();
