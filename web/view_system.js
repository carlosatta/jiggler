// Sistema: token, network, Bluetooth identity, device info, restart.
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr;
  var clearErrors = App.clearErrors, showError = App.showError;

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
        say('Token changed. This browser already uses the new one; remember to update other clients and MCP configurations.', 'good', 'tokMsg');
        return;
      }
      var d = res.data;
      if (!d.field || !showError(root, d.field, d.message)) say(tr(d.message) || 'Could not change the token.', 'bad', 'tokMsg');
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
    var root = $('view-system');
    ['mode', 'apSsid', 'apPassword'].forEach(function (k) { root.querySelector('[data-err="' + k + '"]').textContent = ''; });
    say('', '', 'netMsg');
    App.call('PUT', '/api/network', { mode: $('netMode').value, apSsid: $('netSsid').value, apPassword: $('netPass').value }).then(function (res) {
      if (res.status === 200) {
        say(restart ? 'Saved. Restarting…' : 'Saved: applies after the next restart.', 'good', 'netMsg');
        if (restart) App.restartDevice();
        return;
      }
      var d = res.data;
      if (!d.field || !showError(root, d.field, d.message)) say(tr(d.message) || 'Could not save.', 'bad', 'netMsg');
    }).catch(function () {});
  }
  $('netSave').onclick = function () { saveNetwork(false); };
  $('netSaveRestart').onclick = function () { saveNetwork(true); };

  // ---- Appearance (how the device introduces itself over Bluetooth) ----
  var profiles = null, device = null, loadedChoice = '', loadedName = '';

  function modelLabel(id) {
    if (id === 'custom') return 'Custom name';
    var list = profiles ? profiles.profiles : [];
    for (var i = 0; i < list.length; i++) if (list[i].id === id) return list[i].label;
    return id;
  }

  function showNameBox() { $('apNameBox').hidden = $('apModel').value !== 'custom'; }

  function renderAppearance() {
    var sel = $('apModel');
    sel.textContent = '';
    profiles.profiles.forEach(function (p) { sel.appendChild(el('option', { value: p.id, text: p.label })); });
    sel.appendChild(el('option', { value: 'custom', text: 'Custom name…' }));
    sel.value = device.profileId;
    $('apName').value = device.name || '';
    loadedChoice = sel.value;
    loadedName = $('apName').value;
    showNameBox();
    var shown = device.profileId === 'custom' ? (device.name || profiles.custom.mouse.name) : modelLabel(device.profileId);
    $('apNow').textContent = 'Currently shown as: ' + shown + '.';
  }

  function loadAppearance() {
    Promise.all([App.call('GET', '/api/profiles'), App.call('GET', '/api/devices')]).then(function (r) {
      profiles = r[0].data;
      device = r[1].data.devices[0];
      renderAppearance();
    }).catch(function () {});
  }
  $('apModel').addEventListener('change', showNameBox);

  App.confirmInline($('apApply'), 'Confirm: restart and pair again', function () {
    var root = $('appearanceBox');
    clearErrors(root);
    say('', '', 'apMsg');
    var choice = $('apModel').value, name = $('apName').value.trim();
    if (choice === loadedChoice && (choice !== 'custom' || name === loadedName)) { say('Nothing to change.', '', 'apMsg'); return; }
    if (choice === 'custom' && !name) { root.querySelector('[data-err="devices[0].name"]').textContent = 'Enter a name.'; return; }
    var next = JSON.parse(JSON.stringify(device));
    next.profileId = choice;
    if (choice === 'custom') next.name = name; else delete next.name;
    App.call('PUT', '/api/devices', { devices: [next] }).then(function (res) {
      if (res.status === 200) { say('Saved. Restarting…', 'good', 'apMsg'); return App.restartDevice(); }
      if (!res.data.field || !showError(root, res.data.field, res.data.message)) say(tr(res.data.message) || 'Could not save.', 'bad', 'apMsg');
    }).catch(function () {});
  });

  // ---- Device info and restart ----
  function renderInfo(s) {
    if (!s.device) return;
    $('infoVersion').textContent = s.device.version;
    $('infoName').textContent = s.device.bleName;
    $('infoAddress').textContent = s.device.bleAddress;
  }
  App.confirmInline($('restartBtn'), 'Confirm: restart', function () { say('', '', 'sysMsg'); App.restartDevice(); });

  App.view('system', {
    load: function () { loadNetwork(); loadAppearance(); if (App.status()) renderInfo(App.status()); },
    onStatus: renderInfo
  });
})();
