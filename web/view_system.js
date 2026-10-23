// Sistema: token, network, Bluetooth identity, device info, restart.
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr, hex4 = JigLib.hex4;
  var labeled = App.labeled, clearErrors = App.clearErrors, showError = App.showError;

  // ---- Token ----
  $('tokenForm').addEventListener('submit', function (ev) {
    ev.preventDefault();
    var root = $('tokenForm');
    clearErrors(root);
    say('', '', 'tokMsg');
    var current = $('tokCurrent').value, next = $('tokNew').value, confirm = $('tokConfirm').value;
    if (!current) { showError(root, 'current', 'the current token is required'); return; }
    var problem = JigLib.checkNewToken(next, confirm);
    if (problem) { root.querySelector('[data-err="' + (next === confirm ? 'token' : 'confirm') + '"]').textContent = problem; return; }
    App.call('PUT', '/api/token', { current: current, token: next }).then(function (res) {
      if (res.status === 200) {
        App.setToken(next);
        $('tokCurrent').value = $('tokNew').value = $('tokConfirm').value = '';
        say('Token cambiato. Questo browser usa già quello nuovo; ricordati di aggiornare gli altri client e le configurazioni MCP.', 'good', 'tokMsg');
        return;
      }
      var d = res.data;
      if (!d.field || !showError(root, d.field, d.message)) say(tr(d.message) || 'Cambio non riuscito.', 'bad', 'tokMsg');
    }).catch(function () {});
  });

  // ---- Network ----
  function loadNetwork() {
    App.call('GET', '/api/network').then(function (res) {
      $('netMode').value = res.data.mode;
      $('netSsid').value = res.data.apSsid;
      $('netPass').value = res.data.apPassword;
    }).catch(function () {});
  }
  function saveNetwork(restart) {
    var root = $('view-sistema');
    ['mode', 'apSsid', 'apPassword'].forEach(function (k) { root.querySelector('[data-err="' + k + '"]').textContent = ''; });
    say('', '', 'netMsg');
    App.call('PUT', '/api/network', { mode: $('netMode').value, apSsid: $('netSsid').value, apPassword: $('netPass').value }).then(function (res) {
      if (res.status === 200) {
        say(restart ? 'Salvato. Riavvio…' : 'Salvato: vale dal prossimo riavvio.', 'good', 'netMsg');
        if (restart) App.restartDevice();
        return;
      }
      var d = res.data;
      if (!d.field || !showError(root, d.field, d.message)) say(tr(d.message) || 'Salvataggio non riuscito.', 'bad', 'netMsg');
    }).catch(function () {});
  }
  $('netSave').onclick = function () { saveNetwork(false); };
  $('netSaveRestart').onclick = function () { saveNetwork(true); };

  // ---- Bluetooth identity ----
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
      card.appendChild(el('h3', { text: d.kind === 'mouse' ? 'Mouse (' + d.id + ')' : 'Tastiera (' + d.id + ')' }));
      var enabled = el('input', { type: 'checkbox' });
      enabled.setAttribute('data-k', 'enabled');
      enabled.checked = d.enabled;
      card.appendChild(el('div', { class: 'field' }, [el('label', { class: 'inline' }, [enabled, el('span', { text: 'Abilitato' })])]));
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
    Promise.all([App.call('GET', '/api/profiles'), App.call('GET', '/api/devices')]).then(function (r) {
      profiles = r[0].data;
      devices = r[1].data.devices;
      renderDevices();
    }).catch(function () {});
  }
  App.confirmInline($('devSave'), 'Conferma: salva e riavvia', function () {
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
    App.call('PUT', '/api/devices', { devices: out }).then(function (res) {
      if (res.status === 200) { say('Salvato. Riavvio…', 'good', 'devMsg'); return App.restartDevice(); }
      if (!res.data.field || !showError(root, res.data.field, res.data.message)) say(tr(res.data.message) || 'Salvataggio non riuscito.', 'bad', 'devMsg');
      else say('Controlla i campi evidenziati.', 'bad', 'devMsg');
    }).catch(function () {});
  });
  $('devBox').addEventListener('toggle', function () { if ($('devBox').open) loadDevices(); });

  // ---- Device info and restart ----
  function renderInfo(s) {
    if (!s.device) return;
    $('infoVersion').textContent = s.device.version;
    $('infoName').textContent = s.device.bleName;
    $('infoAddress').textContent = s.device.bleAddress;
  }
  App.confirmInline($('restartBtn'), 'Conferma: riavvia', function () { say('', '', 'sysMsg'); App.restartDevice(); });

  App.view('sistema', {
    load: function () { loadNetwork(); if ($('devBox').open) loadDevices(); if (App.status()) renderInfo(App.status()); },
    onStatus: renderInfo
  });
})();
