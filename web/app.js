(function () {
  'use strict';
  var tr = JigLib.tr, fmtDuration = JigLib.fmtDuration, fmtAgo = JigLib.fmtAgo, hex4 = JigLib.hex4;
  var KEY = 'jigglerToken';
  var token = localStorage.getItem(KEY) || '';
  var status = null, receivedAt = 0, dirty = {};
  var FIELDS = [
    { id: 'minIntervalSec', scale: 1 }, { id: 'maxIntervalSec', scale: 1 }, { id: 'amplitudePx', scale: 1 },
    { id: 'clickProbability', scale: 100 }, { id: 'scrollProbability', scale: 100 }
  ];
  var $ = function (id) { return document.getElementById(id); };

  function banner(text) { var b = $('banner'); b.textContent = text; b.style.display = text ? 'block' : 'none'; }
  function say(text, kind, id) { var m = $(id || 'msg'); m.textContent = text; m.className = 'msg ' + (kind || ''); }
  function setToken(t) { token = t; if (t) localStorage.setItem(KEY, t); else localStorage.removeItem(KEY); }
  function showLogin(error) {
    $('loginErr').textContent = error || '';
    $('login').style.display = 'flex';
    $('tokenInput').value = '';
    $('tokenInput').focus();
  }

  // body: object (sent as JSON) or string (sent as is)
  function call(method, path, body) {
    var opts = { method: method, headers: { 'Authorization': 'Bearer ' + token } };
    if (body !== undefined) {
      opts.headers['Content-Type'] = 'application/json';
      opts.body = typeof body === 'string' ? body : JSON.stringify(body);
    }
    return fetch(path, opts).then(function (r) {
      if (r.status === 401) { setToken(''); showLogin('Token non valido.'); throw new Error('unauthorized'); }
      return r.json().catch(function () { return {}; }).then(function (data) {
        banner('');
        return { status: r.status, data: data };
      });
    }, function (e) { banner('Dispositivo non raggiungibile.'); throw e; });
  }

  function el(tag, attrs, kids) {
    var e = document.createElement(tag);
    Object.keys(attrs || {}).forEach(function (k) {
      if (k === 'class') e.className = attrs[k];
      else if (k === 'text') e.textContent = attrs[k];
      else e.setAttribute(k, attrs[k]);
    });
    (kids || []).forEach(function (c) { e.appendChild(c); });
    return e;
  }
  var uid = 0;
  function labeled(text, input, errKey) {
    var id = 'f' + (++uid);
    input.id = id;
    var box = el('div', { class: 'field' }, [el('label', { 'for': id, text: text }), input]);
    var err = el('div', { class: 'err' });
    err.setAttribute('data-err', errKey);
    box.appendChild(err);
    return box;
  }
  function clearErrors(root) { root.querySelectorAll('[data-err]').forEach(function (e) { e.textContent = ''; }); }
  function showError(root, field, message) {
    var e = root.querySelector('[data-err="' + field + '"]');
    if (e) { e.textContent = tr(message); return true; }
    return false;
  }
  function restartDevice(msgId) {
    return call('POST', '/api/restart').then(function () {
      say('Riavvio in corso. La pagina torna raggiungibile tra pochi secondi.', 'good', msgId);
    }).catch(function () {});
  }

  function setChip(id, textId, on, label) {
    $(id).className = 'chip' + (on ? ' on' : '');
    $(textId).textContent = label + (on ? ': connesso' : ': non connesso');
  }

  // ---- Stato -------------------------------------------------------------------------------
  function populateForm() {
    if (!status) return;
    FIELDS.forEach(function (f) {
      if (dirty[f.id]) return;
      $(f.id).value = String(Math.round(status.config[f.id] * f.scale));
    });
  }

  function render() {
    if (!status) return;
    var s = status;
    setChip('chipMouse', 'txtMouse', s.mouseConnected, 'Mouse');
    setChip('chipWifi', 'txtWifi', s.wifiConnected, 'WiFi');
    $('state').textContent = { running: 'In esecuzione', paused: 'In pausa', stopped: 'Fermo' }[s.state] || s.state;
    $('count').textContent = s.moveCount;
    if (s.lastMoveAt !== null) $('last').textContent = new Date(s.lastMoveAt * 1000).toLocaleTimeString('it-IT');
    else if (s.lastMoveAgoMs !== null) $('last').textContent = fmtAgo(s.lastMoveAgoMs);
    else $('last').textContent = 'mai';
    $('bStart').disabled = s.state === 'running';
    $('bPause').disabled = s.state !== 'running';
    $('bStop').disabled = s.state === 'stopped';
    $('bMove').disabled = !s.mouseConnected;
    populateForm();
    tick();
  }

  function tick() {
    if (!status) return;
    var el = $('next');
    if (status.state === 'paused') el.textContent = 'Il prossimo movimento riprende con Avvia.';
    else if (status.state === 'stopped') el.textContent = 'Nessun movimento programmato.';
    else if (!status.mouseConnected && status.msToNextMove === 0) el.textContent = 'In attesa che il mouse si colleghi al computer.';
    else {
      var left = Math.max(0, status.msToNextMove - (performance.now() - receivedAt));
      el.textContent = 'Prossimo movimento tra ' + fmtDuration(left);
    }
  }

  var inFlight = false;
  function poll() {
    if (!token || inFlight) return;
    inFlight = true;
    call('GET', '/api/status').then(function (res) {
      status = res.data;
      receivedAt = performance.now();
      render();
    }).catch(function () {}).then(function () { inFlight = false; });
  }

  function action(method, path, okText) {
    return call(method, path).then(function (res) {
      if (res.status >= 400) say(res.data.error === 'mouse_not_connected' ? 'Il mouse non è collegato al computer.' : 'Operazione non riuscita.', 'bad');
      else say(okText || '', 'good');
      poll();
    }).catch(function () {});
  }

  $('bStart').onclick = function () { action('POST', '/api/start', 'Avviato.'); };
  $('bPause').onclick = function () { action('POST', '/api/pause', 'In pausa.'); };
  $('bStop').onclick = function () { action('POST', '/api/stop', 'Fermato.'); };
  $('bMove').onclick = function () {
    action('POST', '/api/move', 'Movimento in corso.');
    setTimeout(poll, 1500);
  };

  FIELDS.forEach(function (f) {
    $(f.id).addEventListener('input', function () { dirty[f.id] = true; });
  });

  function fieldError(id, text) { var e = $('e_' + id); if (e) e.textContent = text; }

  $('cfg').addEventListener('submit', function (ev) {
    ev.preventDefault();
    FIELDS.forEach(function (f) { fieldError(f.id, ''); });
    if (!status) return;
    var body = {}, invalid = false;
    FIELDS.forEach(function (f) {
      var raw = $(f.id).value.trim().replace(',', '.');
      if (raw === '') { fieldError(f.id, 'Campo obbligatorio.'); invalid = true; return; }
      var n = Number(raw);
      if (!isFinite(n)) { fieldError(f.id, 'Inserisci un numero.'); invalid = true; return; }
      if (n !== Math.round(status.config[f.id] * f.scale)) body[f.id] = n / f.scale;
    });
    if (invalid) return;
    say('', '', 'cfgMsg');
    if (Object.keys(body).length === 0) { say('Nessuna modifica da salvare.', '', 'cfgMsg'); return; }
    call('PUT', '/api/config', body).then(function (res) {
      if (res.status === 200) { dirty = {}; say('Configurazione salvata.', 'good', 'cfgMsg'); poll(); return; }
      var d = res.data;
      if (d.field && $('e_' + d.field)) fieldError(d.field, tr(d.message));
      else say('Salvataggio non riuscito.', 'bad', 'cfgMsg');
    }).catch(function () {});
  });

  // ---- Dispositivi -------------------------------------------------------------------------
  var profiles = null, devices = [];


  function profileFor(kind, id) {
    if (id !== 'custom') {
      var list = profiles.profiles;
      for (var i = 0; i < list.length; i++) if (list[i].id === id) return list[i];
    }
    var c = profiles.custom[kind];
    return { id: 'custom', name: c.name, manufacturer: c.manufacturer, vid: c.vid, pid: c.pid, oui: null };
  }

  function refreshPlaceholders(card, kind) {
    var p = profileFor(kind, card.querySelector('[data-k=profileId]').value);
    card.querySelector('[data-k=name]').placeholder = p.name;
    card.querySelector('[data-k=manufacturer]').placeholder = p.manufacturer;
    card.querySelector('[data-k=vid]').placeholder = hex4(p.vid);
    card.querySelector('[data-k=pid]').placeholder = hex4(p.pid);
    card.querySelector('[data-k=mac]').placeholder = p.oui ? p.oui + ':xx:xx:xx (il resto dal chip)' : 'indirizzo del chip (nessuna sostituzione)';
  }

  function renderDevices() {
    var box = $('devList');
    box.textContent = '';
    devices.forEach(function (d, i) {
      var card = el('div', { class: 'dev' });
      card.setAttribute('data-index', String(i));
      card.appendChild(el('h3', { text: d.kind === 'mouse' ? 'Mouse (' + d.id + ')' : 'Tastiera (' + d.id + ')' }));

      var enabled = el('input', { type: 'checkbox' });
      enabled.setAttribute('data-k', 'enabled');
      enabled.checked = d.enabled;
      var enabledLabel = el('label', { class: 'inline' }, [enabled, el('span', { text: 'Abilitato' })]);
      card.appendChild(el('div', { class: 'field' }, [enabledLabel]));

      var sel = el('select');
      sel.setAttribute('data-k', 'profileId');
      profiles.profiles.filter(function (p) { return p.kind === d.kind; }).forEach(function (p) {
        sel.appendChild(el('option', { value: p.id, text: p.label }));
      });
      sel.appendChild(el('option', { value: 'custom', text: 'Personalizzato' }));
      sel.value = d.profileId;
      card.appendChild(labeled('Profilo', sel, 'devices[' + i + '].profileId'));

      [['name', 'Nome Bluetooth'], ['manufacturer', 'Produttore'], ['vid', 'VID'], ['pid', 'PID'], ['mac', 'Indirizzo Bluetooth (MAC)']].forEach(function (f) {
        var inp = el('input', { autocomplete: 'off' });
        inp.setAttribute('data-k', f[0]);
        var cur = d[f[0]];
        if (cur !== undefined && cur !== null) inp.value = (f[0] === 'vid' || f[0] === 'pid') ? hex4(cur) : cur;
        card.appendChild(labeled(f[1], inp, 'devices[' + i + '].' + f[0]));
      });
      sel.addEventListener('change', function () { refreshPlaceholders(card, d.kind); });
      refreshPlaceholders(card, d.kind);
      box.appendChild(card);
    });
  }

  function loadDevices() {
    Promise.all([call('GET', '/api/profiles'), call('GET', '/api/devices')]).then(function (r) {
      profiles = r[0].data;
      devices = r[1].data.devices;
      renderDevices();
    }).catch(function () {});
  }

  $('devSave').onclick = function () {
    var root = $('devList');
    clearErrors(root);
    say('', '', 'devMsg');
    var out = [], bad = false;
    root.querySelectorAll('.dev').forEach(function (card, i) {
      var base = devices[i];
      var d = { id: base.id, kind: base.kind, enabled: card.querySelector('[data-k=enabled]').checked, profileId: card.querySelector('[data-k=profileId]').value };
      ['name', 'manufacturer', 'mac'].forEach(function (k) {
        var v = card.querySelector('[data-k=' + k + ']').value.trim();
        if (v !== '') d[k] = v;
      });
      ['vid', 'pid'].forEach(function (k) {
        var v = card.querySelector('[data-k=' + k + ']').value.trim();
        if (v === '') return;
        var n = Number(v);
        if (!isFinite(n) || Math.floor(n) !== n) { showError(root, 'devices[' + i + '].' + k, 'must be an integer from 0 to 65535'); bad = true; return; }
        d[k] = n;
      });
      out.push(d);
    });
    if (bad) return;
    call('PUT', '/api/devices', { devices: out }).then(function (res) {
      if (res.status === 200) { say('Salvato. Riavvio…', 'good', 'devMsg'); return restartDevice('devMsg'); }
      if (!res.data.field || !showError(root, res.data.field, res.data.message)) say(tr(res.data.message) || 'Salvataggio non riuscito.', 'bad', 'devMsg');
      else say('Controlla i campi evidenziati.', 'bad', 'devMsg');
    }).catch(function () {});
  };

  // ---- Script ------------------------------------------------------------------------------
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
    call('GET', '/api/scripts/' + encodeURIComponent(name)).then(function (res) {
      if (res.status !== 200) { say('Script non trovato.', 'bad', 'scrMsg'); return; }
      isNew = false;
      builtin = res.data.builtin;
      $('scrName').value = res.data.name;
      $('scrBody').value = JSON.stringify(res.data.script, null, 2);
      clearErrors($('tab-script'));
      setScriptButtons();
    }).catch(function () {});
  }

  function loadScripts(selected) {
    call('GET', '/api/scripts').then(function (res) {
      scriptList = res.data.scripts;
      renderScriptSelect(selected);
      if (scriptList.length && !isNew) openScript($('scrSelect').value);
      else setScriptButtons();
    }).catch(function () {});
  }

  function scriptError(res) {
    var d = res.data, root = $('tab-script');
    clearErrors(root);
    if (d.field === 'name') { $('e_scrName').textContent = tr(d.message); return; }
    $('e_scrBody').textContent = (d.field ? d.field + ': ' : '') + tr(d.message);
  }

  $('scrSelect').addEventListener('change', function () { isNew = false; openScript($('scrSelect').value); });
  $('scrNew').onclick = function () {
    isNew = true; builtin = false;
    $('scrName').value = '';
    $('scrBody').value = TEMPLATE;
    clearErrors($('tab-script'));
    say('', '', 'scrMsg');
    setScriptButtons();
    $('scrName').focus();
  };
  $('scrSave').onclick = function () {
    var name = $('scrName').value.trim();
    say('', '', 'scrMsg');
    clearErrors($('tab-script'));
    if (!name) { $('e_scrName').textContent = 'Inserisci un nome.'; return; }
    call('PUT', '/api/scripts/' + encodeURIComponent(name), $('scrBody').value).then(function (res) {
      if (res.status === 200) { isNew = false; say('Script salvato.', 'good', 'scrMsg'); loadScripts(name); }
      else scriptError(res);
    }).catch(function () {});
  };
  $('scrRun').onclick = function () {
    call('POST', '/api/scripts/' + encodeURIComponent($('scrName').value.trim()) + '/run').then(function (res) {
      if (res.status === 202) say('Script avviato.', 'good', 'scrMsg');
      else say(res.data.error === 'mouse_not_connected' ? 'Il dispositivo non è collegato al computer.' : 'Esecuzione non riuscita.', 'bad', 'scrMsg');
    }).catch(function () {});
  };
  $('scrDelete').onclick = function () {
    var name = $('scrName').value.trim();
    call('DELETE', '/api/scripts/' + encodeURIComponent(name)).then(function (res) {
      if (res.status === 200) { isNew = false; say('Script eliminato.', 'good', 'scrMsg'); loadScripts(); }
      else say(tr(res.data.message) || 'Eliminazione non riuscita.', 'bad', 'scrMsg');
    }).catch(function () {});
  };

  // ---- Automazioni -------------------------------------------------------------------------
  var autos = [];

  function renderAutos() {
    var box = $('autoList');
    box.textContent = '';
    autos.forEach(function (a, i) {
      var row = el('div', { class: 'dev' });
      row.setAttribute('data-index', String(i));
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
      out.push({ id: g('id').value.trim(), script: g('script').value, minIntervalSec: Number(g('minIntervalSec').value), maxIntervalSec: Number(g('maxIntervalSec').value), enabled: g('enabled').checked });
    });
    autos = out;
    return out;
  }

  function loadAutos() {
    Promise.all([call('GET', '/api/scripts'), call('GET', '/api/automations')]).then(function (r) {
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
    call('PUT', '/api/automations', { automations: collectAutos() }).then(function (res) {
      if (res.status === 200) { autos = res.data.automations; renderAutos(); say('Automazioni salvate.', 'good', 'autoMsg'); return; }
      if (!res.data.field || !showError(root, res.data.field, res.data.message)) say(tr(res.data.message) || 'Salvataggio non riuscito.', 'bad', 'autoMsg');
      else say('Controlla i campi evidenziati.', 'bad', 'autoMsg');
    }).catch(function () {});
  };

  // ---- Rete --------------------------------------------------------------------------------
  function loadNetwork() {
    call('GET', '/api/network').then(function (res) {
      $('netMode').value = res.data.mode;
      $('netSsid').value = res.data.apSsid;
      $('netPass').value = res.data.apPassword;
    }).catch(function () {});
  }
  function saveNetwork(restart) {
    clearErrors($('tab-rete'));
    say('', '', 'netMsg');
    var body = { mode: $('netMode').value, apSsid: $('netSsid').value, apPassword: $('netPass').value };
    call('PUT', '/api/network', body).then(function (res) {
      if (res.status === 200) {
        say('Salvato.', 'good', 'netMsg');
        if (restart) return restartDevice('netMsg');
        return;
      }
      var d = res.data;
      if (!d.field || !showError($('tab-rete'), d.field, d.message)) say(tr(d.message) || 'Salvataggio non riuscito.', 'bad', 'netMsg');
    }).catch(function () {});
  }
  $('netSave').onclick = function () { saveNetwork(false); };
  $('netSaveRestart').onclick = function () { saveNetwork(true); };

  // ---- Tabs and login ----------------------------------------------------------------------
  var LOADERS = { dispositivi: loadDevices, script: function () { loadScripts(); }, automazioni: loadAutos, rete: loadNetwork };
  function showTab(name) {
    document.querySelectorAll('#tabs button').forEach(function (b) { b.setAttribute('aria-selected', b.getAttribute('data-tab') === name ? 'true' : 'false'); });
    document.querySelectorAll('.tab').forEach(function (t) { t.className = 'tab' + (t.id === 'tab-' + name ? ' on' : ''); });
    if (LOADERS[name] && token) LOADERS[name]();
  }
  document.querySelectorAll('#tabs button').forEach(function (b) {
    b.addEventListener('click', function () { showTab(b.getAttribute('data-tab')); });
  });

  $('loginForm').addEventListener('submit', function (ev) {
    ev.preventDefault();
    var t = $('tokenInput').value.trim();
    if (!t) { $('loginErr').textContent = 'Inserisci il token.'; return; }
    setToken(t);
    $('login').style.display = 'none';
    poll();
  });

  if (!token) showLogin('');
  poll();
  setInterval(poll, 1000);
  setInterval(tick, 250);
})();
