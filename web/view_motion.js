// Movimento: random-movement configuration, scripts and automations.
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
      if (raw === '') { fieldError(f.id, 'Campo obbligatorio.'); invalid = true; return; }
      var n = Number(raw);
      if (!isFinite(n)) { fieldError(f.id, 'Inserisci un numero.'); invalid = true; return; }
      if (n !== Math.round(config[f.id] * f.scale)) body[f.id] = n / f.scale;
    });
    if (invalid) return;
    say('', '', 'cfgMsg');
    if (Object.keys(body).length === 0) { say('Nessuna modifica da salvare.', '', 'cfgMsg'); return; }
    App.call('PUT', '/api/config', body).then(function (res) {
      if (res.status === 200) { dirty = {}; say('Configurazione salvata.', 'good', 'cfgMsg'); App.poll(); return; }
      var d = res.data;
      if (d.field && $('e_' + d.field)) { fieldError(d.field, tr(d.message)); $(d.field).setAttribute('aria-invalid', 'true'); }
      else say('Salvataggio non riuscito.', 'bad', 'cfgMsg');
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
    scriptList.forEach(function (s) { sel.appendChild(el('option', { value: s.name, text: s.name + (s.builtin ? ' (predefinito)' : '') })); });
    if (selected) sel.value = selected;
  }
  function openScript(name) {
    App.call('GET', '/api/scripts/' + encodeURIComponent(name)).then(function (res) {
      if (res.status !== 200) { say('Script non trovato.', 'bad', 'scrMsg'); return; }
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
    if (!name) { $('e_scrName').textContent = 'Inserisci un nome.'; return; }
    App.call('PUT', '/api/scripts/' + encodeURIComponent(name), $('scrBody').value).then(function (res) {
      if (res.status === 200) { isNew = false; say('Script salvato.', 'good', 'scrMsg'); loadScripts(name); }
      else scriptError(res);
    }).catch(function () {});
  };
  $('scrRun').onclick = function () {
    App.call('POST', '/api/scripts/' + encodeURIComponent($('scrName').value.trim()) + '/run').then(function (res) {
      if (res.status === 202) say('Script avviato.', 'good', 'scrMsg');
      else say(res.data.error === 'mouse_not_connected' ? 'Nessun computer collegato.' : 'Esecuzione non riuscita.', 'bad', 'scrMsg');
    }).catch(function () {});
  };
  App.confirmInline($('scrDelete'), 'Conferma: elimina', function () {
    App.call('DELETE', '/api/scripts/' + encodeURIComponent($('scrName').value.trim())).then(function (res) {
      if (res.status === 200) { isNew = false; say('Script eliminato.', 'good', 'scrMsg'); loadScripts(); }
      else say(tr(res.data.message) || 'Eliminazione non riuscita.', 'bad', 'scrMsg');
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
      row.appendChild(labeled('Nome', id, 'automations[' + i + '].id'));
      var sel = el('select');
      sel.setAttribute('data-k', 'script');
      scriptList.forEach(function (s) { sel.appendChild(el('option', { value: s.name, text: s.name })); });
      if (!scriptList.some(function (s) { return s.name === a.script; })) sel.appendChild(el('option', { value: a.script, text: a.script + ' (mancante)' }));
      sel.value = a.script;
      row.appendChild(labeled('Script', sel, 'automations[' + i + '].script'));
      var pair = el('div', { class: 'row' });
      [['minIntervalSec', 'Minimo (s)'], ['maxIntervalSec', 'Massimo (s)']].forEach(function (f) {
        var inp = el('input', { inputmode: 'numeric', autocomplete: 'off' });
        inp.setAttribute('data-k', f[0]);
        inp.value = String(a[f[0]]);
        pair.appendChild(labeled(f[1], inp, 'automations[' + i + '].' + f[0]));
      });
      row.appendChild(pair);
      var en = el('input', { type: 'checkbox' });
      en.setAttribute('data-k', 'enabled');
      en.checked = a.enabled;
      row.appendChild(el('div', { class: 'field' }, [el('label', { class: 'inline' }, [en, el('span', { text: 'Attiva' })])]));
      var rm = el('button', { class: 'danger', text: 'Rimuovi' });
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
      if (res.status === 200) { autos = res.data.automations; renderAutos(); say('Automazioni salvate.', 'good', 'autoMsg'); return; }
      if (!res.data.field || !showError(root, res.data.field, res.data.message)) say(tr(res.data.message) || 'Salvataggio non riuscito.', 'bad', 'autoMsg');
      else say('Controlla i campi evidenziati.', 'bad', 'autoMsg');
    }).catch(function () {});
  };

  $('scriptsBox').addEventListener('toggle', function () { if ($('scriptsBox').open) loadScripts(); });
  $('autoBox').addEventListener('toggle', function () { if ($('autoBox').open) loadAutos(); });

  App.view('movimento', {
    load: function () {
      if (App.status()) populate(App.status());
      if ($('scriptsBox').open) loadScripts();
      if ($('autoBox').open) loadAutos();
    },
    onStatus: function (s) { populate(s); }
  });
})();
