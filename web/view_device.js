// Device: how the movement behaves, and how the device appears over Bluetooth.
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr, hex4 = JigLib.hex4;
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

  // ---- Appearance (how the device introduces itself over Bluetooth) ----
  var appearance = null, loadedKey = '';

  function profileInfo(id) {
    var list = appearance ? appearance.profiles : [];
    for (var i = 0; i < list.length; i++) if (list[i].id === id) return list[i];
    var c = appearance.custom;
    return { id: 'custom', name: c.name, manufacturer: c.manufacturer, vid: c.vid, pid: c.pid, oui: null };
  }

  function showPlaceholders() {
    var p = profileInfo($('apModel').value);
    $('apName').placeholder = p.name;
    $('apManufacturer').placeholder = p.manufacturer;
    $('apVid').placeholder = hex4(p.vid);
    $('apPid').placeholder = hex4(p.pid);
    $('apMac').placeholder = p.oui ? p.oui + ':xx:xx:xx (the rest from the chip)' : 'the chip’s own address';
  }

  function parseId(text) {
    var t = text.trim();
    if (/^0x[0-9a-f]{1,4}$/i.test(t)) return parseInt(t.slice(2), 16);
    if (/^\d{1,5}$/.test(t) && Number(t) <= 65535) return Number(t);
    return null;
  }

  // The request body for what the form shows now; { error: [field, message] } when a field cannot be read.
  function readAppearance() {
    var body = { profile: $('apModel').value };
    var name = $('apName').value.trim(), man = $('apManufacturer').value.trim(), mac = $('apMac').value.trim();
    if (name) body.name = name;
    if (man) body.manufacturer = man;
    if (mac) body.mac = mac;
    var ids = [['vid', 'apVid'], ['pid', 'apPid']];
    for (var i = 0; i < ids.length; i++) {
      var raw = $(ids[i][1]).value.trim();
      if (!raw) continue;
      var n = parseId(raw);
      if (n === null) return { error: [ids[i][0], 'must be an integer from 0 to 65535'] };
      body[ids[i][0]] = n;
    }
    return { body: body };
  }

  function renderAppearance() {
    var sel = $('apModel');
    sel.textContent = '';
    sel.appendChild(el('option', { value: 'custom', text: 'Custom' }));
    appearance.profiles.forEach(function (p) { sel.appendChild(el('option', { value: p.id, text: p.label })); });
    sel.value = appearance.profile;
    $('apName').value = appearance.name || '';
    $('apManufacturer').value = appearance.manufacturer || '';
    $('apVid').value = appearance.vid !== undefined ? hex4(appearance.vid) : '';
    $('apPid').value = appearance.pid !== undefined ? hex4(appearance.pid) : '';
    $('apMac').value = appearance.mac || '';
    showPlaceholders();
    loadedKey = JSON.stringify(readAppearance().body);
    $('apNow').textContent = 'Currently shown as: ' + appearance.shownAs + '.';
  }

  function loadAppearance() {
    App.call('GET', '/api/appearance').then(function (res) {
      if (res.status !== 200) return;
      appearance = res.data;
      renderAppearance();
    }).catch(function () {});
  }
  $('apModel').addEventListener('change', showPlaceholders);

  App.confirmInline($('apApply'), 'Confirm: restart and pair again', function () {
    var root = $('appearanceBox');
    clearErrors(root);
    say('', '', 'apMsg');
    var read = readAppearance();
    if (read.error) { showError(root, read.error[0], read.error[1]); return; }
    if (JSON.stringify(read.body) === loadedKey) { say('Nothing to change.', '', 'apMsg'); return; }
    App.call('PUT', '/api/appearance', read.body).then(function (res) {
      if (res.status === 200) { say('Saved. Restarting…', 'good', 'apMsg'); return App.restartDevice(); }
      if (!res.data.field || !showError(root, res.data.field, res.data.message)) say(tr(res.data.message) || 'Could not save.', 'bad', 'apMsg');
    }).catch(function () {});
  });

  App.view('device', {
    load: function () { if (App.status()) populate(App.status()); loadAppearance(); },
    onStatus: function (s) { populate(s); }
  });
})();
