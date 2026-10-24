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
  var appearance = null, loadedProfile = '', loadedName = '';

  function modelLabel(id) {
    var list = appearance ? appearance.profiles : [];
    for (var i = 0; i < list.length; i++) if (list[i].id === id) return list[i].label;
    return id;
  }

  function showNameBox() { $('apNameBox').hidden = $('apModel').value !== 'custom'; }

  function renderAppearance() {
    var sel = $('apModel');
    sel.textContent = '';
    sel.appendChild(el('option', { value: 'custom', text: 'Custom name' }));
    appearance.profiles.forEach(function (p) { sel.appendChild(el('option', { value: p.id, text: p.label })); });
    sel.value = appearance.profile;
    $('apName').placeholder = appearance.custom.name;
    $('apName').value = appearance.name || '';
    loadedProfile = sel.value;
    loadedName = $('apName').value;
    showNameBox();
    $('apNow').textContent = 'Currently shown as: ' + appearance.shownAs + '.';
  }

  function loadAppearance() {
    App.call('GET', '/api/appearance').then(function (res) {
      if (res.status !== 200) return;
      appearance = res.data;
      renderAppearance();
    }).catch(function () {});
  }
  $('apModel').addEventListener('change', showNameBox);

  App.confirmInline($('apApply'), 'Confirm: restart and pair again', function () {
    var root = $('appearanceBox');
    clearErrors(root);
    say('', '', 'apMsg');
    var profile = $('apModel').value, name = $('apName').value.trim();
    if (profile === loadedProfile && (profile !== 'custom' || name === loadedName)) { say('Nothing to change.', '', 'apMsg'); return; }
    var next = { profile: profile };
    ['manufacturer', 'vid', 'pid', 'mac'].forEach(function (k) { if (appearance[k] !== undefined) next[k] = appearance[k]; });  // advanced values set through the API stay
    if (profile === 'custom' && name) next.name = name;
    App.call('PUT', '/api/appearance', next).then(function (res) {
      if (res.status === 200) { say('Saved. Restarting…', 'good', 'apMsg'); return App.restartDevice(); }
      if (!res.data.field || !showError(root, res.data.field, res.data.message)) say(tr(res.data.message) || 'Could not save.', 'bad', 'apMsg');
    }).catch(function () {});
  });

  // ---- Firmware update (OTA) ----
  function uploadFirmware(file) {
    var xhr = new XMLHttpRequest();
    var bar = $('fwProgress');
    bar.hidden = false;
    bar.value = 0;
    $('fwUpload').disabled = true;
    say('Uploading ' + Math.round(file.size / 1024) + ' KB…', '', 'fwMsg');
    xhr.upload.onprogress = function (e) { if (e.lengthComputable) bar.value = Math.round((e.loaded / e.total) * 100); };
    xhr.onload = function () {
      var data = {};
      try { data = JSON.parse(xhr.responseText); } catch (e) {}
      $('fwUpload').disabled = false;
      if (xhr.status === 200) {
        bar.value = 100;
        say('Updated. Restarting…', 'good', 'fwMsg');
        App.waitForDevice();
        return;
      }
      bar.hidden = true;
      if (xhr.status === 401) say('The token was refused.', 'bad', 'fwMsg');
      else say(tr(data.message) || 'The update failed.', 'bad', 'fwMsg');
    };
    xhr.onerror = function () { bar.hidden = true; $('fwUpload').disabled = false; say('Could not reach the device.', 'bad', 'fwMsg'); };
    var form = new FormData();
    form.append('firmware', file, file.name);
    xhr.open('POST', '/api/update');
    xhr.setRequestHeader('Authorization', 'Bearer ' + App.getToken());
    xhr.send(form);
  }
  App.confirmInline($('fwUpload'), 'Confirm: upload and restart', function () {
    $('e_fwFile').textContent = '';
    say('', '', 'fwMsg');
    var file = $('fwFile').files[0];
    if (!file) { $('e_fwFile').textContent = 'Choose a firmware file first.'; return; }
    if (!/\.bin$/i.test(file.name)) { $('e_fwFile').textContent = 'That does not look like a firmware file (.bin).'; return; }
    uploadFirmware(file);
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
