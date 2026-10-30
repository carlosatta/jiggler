// System: token, time, button, network, firmware update, device info, restart.
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

  // ---- Time ----
  var timeDirty = false;
  function renderTimeNow(s) { if (s.time) $('timeNow').textContent = JigLib.clockLine(s.time); }
  function loadTime() {
    App.call('GET', '/api/time').then(function (res) {
      if (res.status !== 200) return;
      var sel = $('tzSelect');
      if (sel.options.length !== res.data.timezones.length) {
        sel.textContent = '';
        res.data.timezones.forEach(function (z) { sel.appendChild(el('option', { value: z, text: z.replace('_', ' ') })); });
      }
      if (!timeDirty) { sel.value = res.data.timezone; $('ntpServer').value = res.data.ntpServer; }
    }).catch(function () {});
  }
  ['tzSelect', 'ntpServer'].forEach(function (id) { $(id).addEventListener('input', function () { timeDirty = true; }); });
  $('timeSave').onclick = function () {
    var root = $('timeBox');
    clearErrors(root);
    say('', '', 'timeMsg');
    App.call('PUT', '/api/time', { timezone: $('tzSelect').value, ntpServer: $('ntpServer').value.trim() }).then(function (res) {
      if (res.status === 200) { timeDirty = false; say('Saved.', 'good', 'timeMsg'); App.poll(); return; }
      var d = res.data;
      if (!d.field || !showError(root, d.field, d.message)) say(tr(d.message) || 'Could not save.', 'bad', 'timeMsg');
    }).catch(function () {});
  };
  $('timeSet').onclick = function () {
    say('', '', 'timeMsg');
    App.call('POST', '/api/time/set', { epoch: Math.floor(Date.now() / 1000) }).then(function (res) {
      if (res.status === 200) { say('The device clock now follows this browser’s time. It is lost when the device restarts unless NTP sets it again.', 'good', 'timeMsg'); App.poll(); return; }
      say(tr(res.data && res.data.message) || 'Could not set the time.', 'bad', 'timeMsg');
    }).catch(function () {});
  };

  // ---- Button ----
  function loadButton() {
    App.call('GET', '/api/button').then(function (res) {
      if (res.status !== 200) return;
      var sel = $('btnGpio');
      if (sel.options.length !== res.data.gpios.length) {
        sel.textContent = '';
        res.data.gpios.forEach(function (g) { sel.appendChild(el('option', { value: String(g), text: 'GPIO ' + g + (g === res.data['default'] ? ' (BOOT button)' : '') })); });
      }
      sel.value = String(res.data.gpio);
    }).catch(function () {});
  }
  $('btnSave').onclick = function () {
    var root = $('buttonBox');
    clearErrors(root);
    say('', '', 'btnMsg');
    App.call('PUT', '/api/button', { gpio: Number($('btnGpio').value) }).then(function (res) {
      if (res.status === 200) { say('Saved. The button works on that pin now.', 'good', 'btnMsg'); return; }
      var d = res.data;
      if (!d.field || !showError(root, d.field, d.message)) say(tr(d.message) || 'Could not save.', 'bad', 'btnMsg');
    }).catch(function () {});
  };

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

  // ---- Firmware update (OTA) ----
  var FW_TRIES = 3;
  function uploadFirmware(file, attempt) {
    attempt = attempt || 1;
    var xhr = new XMLHttpRequest();
    var bar = $('fwProgress');
    bar.hidden = false;
    bar.value = 0;
    $('fwUpload').disabled = true;
    say('Uploading ' + Math.round(file.size / 1024) + ' KB' + (attempt > 1 ? ' (attempt ' + attempt + ' of ' + FW_TRIES + ')' : '') + '…', '', 'fwMsg');
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
    xhr.onerror = function () {
      // The radio can drop the connection mid-upload; nothing is installed until the whole file arrived, so try again.
      if (attempt < FW_TRIES) { say('The connection dropped. Trying again…', '', 'fwMsg'); setTimeout(function () { uploadFirmware(file, attempt + 1); }, 2500); return; }
      bar.hidden = true;
      $('fwUpload').disabled = false;
      say('Could not finish the upload after ' + FW_TRIES + ' attempts. Move closer to the router or try again.', 'bad', 'fwMsg');
    };
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

  // ---- Firmware updates from the repository ----
  var upd = null, updTimer = null, updDirty = false, restartWatching = false;
  var MODE_HINTS = {
    manual: 'The device looks once a day, at the time below, and tells you here and on Status. You press Install.',
    automatic: 'The device looks once a day, at the time below, and installs a new version by itself, then restarts (the jiggler and Bluetooth are off for a short while). A version that did not take effect is not retried automatically.',
    ignore: 'The device never looks by itself. You can still press Check for updates.'
  };
  function showTimeField() {
    var off = $('updMode').value === 'ignore';
    $('updTimeBox').hidden = off;
    $('updTimeHint').hidden = off;
  }
  function renderUpdates() {
    if (!upd) return;
    var d = JigLib.describeUpdate(upd);
    if (upd.current) $('updVersion').textContent = 'Running version ' + upd.current + '.';
    $('updInfo').textContent = d.text;
    $('updProgress').hidden = upd.state !== 'installing';
    $('updProgress').value = upd.progress || 0;
    $('updCheck').disabled = d.working;
    $('updInstall').hidden = !d.canInstall;
    $('updInstall').disabled = d.working;
    $('updInstall').textContent = upd.latest ? 'Install ' + upd.latest.version : 'Install';
    var notes = $('updNotes');
    notes.hidden = !(upd.latest && upd.latest.pageUrl);
    if (!notes.hidden) notes.href = upd.latest.pageUrl;
    if (!updDirty) {
      $('updMode').value = upd.mode;
      $('updRepo').value = upd.repoUrl;
      $('updTime').value = upd.checkTime;
    }
    $('updModeHint').textContent = MODE_HINTS[$('updMode').value] || '';
    showTimeField();
    var repoLink = $('infoRepo');
    repoLink.href = upd.repoUrl;
    repoLink.textContent = JigLib.repoLabel(upd.repoUrl);
    if (upd.state === 'restarting' && !restartWatching) { restartWatching = true; App.waitForDevice(); }
    clearTimeout(updTimer);
    // While an install runs the device is left alone: App.poll asks for the progress every few seconds and hands
    // it here through onUpdate, so this view does not ask for it as well.
    if (d.working && !App.installing() && !$('view-system').hidden) updTimer = setTimeout(loadUpdates, 1500);
  }
  function loadUpdates() {
    if (App.installing()) return;  // App.poll is the only one asking while an install runs; it feeds onUpdate
    App.call('GET', '/api/updates').then(function (res) {
      if (res.status !== 200) return;
      upd = JigLib.mergeUpdate(upd, res.data);  // while installing the device answers with state and progress only
      renderUpdates();
    }).catch(function () {});
  }
  ['updMode', 'updRepo', 'updTime'].forEach(function (id) {
    $(id).addEventListener('input', function () { updDirty = true; $('updModeHint').textContent = MODE_HINTS[$('updMode').value] || ''; showTimeField(); });
  });
  $('updCheck').onclick = function () {
    say('', '', 'updMsg');
    App.call('POST', '/api/updates/check').then(function (res) {
      if (res.status === 202) { upd = res.data; renderUpdates(); updTimer = setTimeout(loadUpdates, 800); return; }
      say(tr(res.data && res.data.message) || 'Could not start the check.', 'bad', 'updMsg');
    }).catch(function () {});
  };
  App.confirmInline($('updInstall'), 'Confirm: install and restart', function () {
    say('', '', 'updMsg');
    App.call('POST', '/api/updates/install').then(function (res) {
      if (res.status === 202) { upd = res.data; renderUpdates(); updTimer = setTimeout(loadUpdates, 800); return; }
      say(tr(res.data && res.data.message) || 'Could not start the install.', 'bad', 'updMsg');
    }).catch(function () {});
  });
  function saveUpdates(repoOverride) {
    var root = $('updBox');
    clearErrors(root);
    say('', '', 'updSaveMsg');
    var body = { mode: $('updMode').value, repoUrl: repoOverride !== undefined ? repoOverride : $('updRepo').value.trim() };
    if ($('updTime').value) body.checkTime = $('updTime').value;
    App.call('PUT', '/api/updates', body).then(function (res) {
      if (res.status === 200) { updDirty = false; upd = res.data; renderUpdates(); say('Saved.', 'good', 'updSaveMsg'); App.poll(); return; }
      var d = res.data;
      if (!d.field || !showError(root, d.field, d.message)) say(tr(d.message) || 'Could not save.', 'bad', 'updSaveMsg');
    }).catch(function () {});
  }
  $('updSave').onclick = function () { saveUpdates(); };
  $('updReset').onclick = function () { saveUpdates(''); };

  // ---- Device info and restart ----
  function renderInfo(s) {
    if (!s.device) return;
    $('infoVersion').textContent = s.device.version;
    $('infoName').textContent = s.device.bleName;
    $('infoAddress').textContent = s.device.bleAddress;
  }
  App.confirmInline($('restartBtn'), 'Confirm: restart', function () { say('', '', 'sysMsg'); App.restartDevice(); });

  App.view('system', {
    load: function () { loadNetwork(); loadTime(); loadButton(); loadUpdates(); if (App.status()) { renderInfo(App.status()); renderTimeNow(App.status()); } },
    onStatus: function (s) { renderInfo(s); renderTimeNow(s); },
    // While an install runs, App.poll is the only thing asking the device anything; this is its answer.
    onUpdate: function (u) { upd = JigLib.mergeUpdate(upd, u); renderUpdates(); }
  });
})();
