// Computers: the Bluetooth state, Discovery (the pairing window) and the list of paired computers (rename, forget).
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr;
  var peers = [], shown = '', editing = null, lastFetch = 0, busy = false;
  var settingsShown = '', settingsDirty = false;

  function reply(res, okText, id) {
    if (res.status === 200) { say(okText, 'good', id); return true; }
    say(tr(res.data && res.data.message) || 'That did not work.', 'bad', id);
    return false;
  }

  var STATE_TEXT = {
    off: 'Off: the radio is silent. Nothing can see or reach the device.',
    waiting: 'Waiting: on. Only computers that are already paired can find the device and connect.',
    discovery: 'Discovery: open, so a new computer can pair.',
    connected: 'Connected to a computer.'
  };

  function renderPairing(status, elapsed) {
    var bt = status && status.bluetooth;
    if (!bt) return;
    var p = bt.pairing || {};
    var timed = p.open && p.remainingMs != null;
    var off = bt.state === 'off';
    $('btInfo').textContent = STATE_TEXT[bt.state] || '';
    $('btToggle').textContent = off ? 'Turn Bluetooth on' : 'Turn Bluetooth off';
    $('btToggle').className = off ? 'primary' : '';
    $('btToggle').disabled = busy;
    $('pairInfo').textContent = timed
      ? 'Discovery is open: closes in ' + JigLib.fmtDuration(Math.max(0, p.remainingMs - elapsed)) + '. It also closes as soon as a computer pairs.'
      : 'Discovery is closed: no new computer can pair.';
    $('pairOpen').disabled = busy || (p.open && !timed);
    $('pairOpen').textContent = timed ? 'Restart Discovery' : 'Start Discovery';
    $('pairClose').disabled = busy || !timed;
    var key = bt.discoverySeconds + '|' + bt.offWhenComputerDisconnects;
    if (key !== settingsShown && !settingsDirty) {
      settingsShown = key;
      $('discSeconds').value = bt.discoverySeconds;
      $('offOnDisc').checked = !!bt.offWhenComputerDisconnects;
    }
  }

  function peerRow(p) {
    var li = el('li');
    if (editing === p.id) {
      var input = el('input', { autocomplete: 'off', 'aria-label': 'Computer name', maxlength: '32' });
      input.value = p.label;
      var save = el('button', { class: 'primary', text: 'Save' });
      var cancel = el('button', { text: 'Cancel' });
      var err = el('div', { class: 'err' });
      function done() { editing = null; shown = ''; draw(); }
      function commit() {
        var name = input.value.trim();
        if (!name) { err.textContent = 'Enter a name.'; return; }
        App.call('PUT', '/api/bluetooth/peers/' + p.id, { label: name }).then(function (res) {
          if (res.status === 200) { done(); load(); } else err.textContent = tr(res.data && res.data.message);
        }).catch(function () {});
      }
      save.onclick = commit;
      cancel.onclick = done;
      input.addEventListener('keydown', function (e) { if (e.key === 'Enter') commit(); if (e.key === 'Escape') done(); });
      li.appendChild(el('div', { class: 'peer-edit' }, [input, save, cancel]));
      li.appendChild(err);
      setTimeout(function () { input.focus(); input.select(); }, 0);
      return li;
    }
    var meta = el('div', { class: 'peer-meta' });
    if (p.connected) meta.appendChild(el('span', { class: 'peer-live', text: 'Connected now' }));
    else meta.appendChild(document.createTextNode('Paired'));
    meta.appendChild(document.createTextNode(' · ' + p.address));
    var rename = el('button', { class: 'link', text: 'Rename' });
    rename.onclick = function () { editing = p.id; shown = ''; draw(); };
    var forget = el('button', { class: 'link danger', text: 'Forget' });
    App.confirmInline(forget, 'Confirm: forget', function () {
      App.call('DELETE', '/api/bluetooth/peers/' + p.id).then(function (res) {
        reply(res, '“' + p.label + '” forgotten. To use it again, pair it from scratch (and remove the device in that computer’s Bluetooth settings).', 'peersMsg');
        load(); App.poll();
      }).catch(function () {});
    });
    li.appendChild(el('div', { class: 'peer-top' }, [
      el('div', {}, [el('div', { class: 'peer-name', text: p.label }), meta]),
      el('div', { class: 'peer-actions' }, [rename, forget])
    ]));
    return li;
  }

  function draw() {
    var key = JSON.stringify(peers) + '|' + editing;
    if (key === shown) return;
    shown = key;
    var list = $('peers');
    list.textContent = '';
    peers.forEach(function (p) { list.appendChild(peerRow(p)); });
    $('peersEmpty').hidden = peers.length > 0;
    $('forgetAll').hidden = peers.length === 0;
  }

  function armed() {
    return !!document.querySelector('#peers .confirm') || $('forgetAll').classList.contains('confirm');
  }

  function load() {
    lastFetch = performance.now();
    App.call('GET', '/api/bluetooth').then(function (res) {
      if (res.status !== 200) return;
      peers = res.data.peers;
      if (editing === null && !armed()) draw();
    }).catch(function () {});
  }

  App.view('computers', {
    load: load,
    onStatus: function (s) {
      renderPairing(s, 0);
      if (!$('view-computers').hidden && performance.now() - lastFetch > 2000) load();
    },
    onTick: function (s, elapsed) { renderPairing(s, elapsed); }
  });

  function run(method, path, okText, id) {
    busy = true; renderPairing(App.status(), 0);
    return App.call(method, path).then(function (res) { reply(res, okText, id); }).catch(function () {}).then(function () { busy = false; App.poll(); });
  }
  $('pairOpen').onclick = function () { run('POST', '/api/bluetooth/pairing', 'Discovery is open. On the computer, choose the device in the Bluetooth list.', 'pairMsg'); };
  $('pairClose').onclick = function () { run('DELETE', '/api/bluetooth/pairing', 'Discovery stopped.', 'pairMsg'); };
  $('btToggle').onclick = function () {
    var off = App.status() && App.status().bluetooth && App.status().bluetooth.state === 'off';
    if (off) run('POST', '/api/bluetooth/enable', 'Bluetooth is on: paired computers reconnect by themselves.', 'btMsg');
    else run('POST', '/api/bluetooth/disable', 'Bluetooth is off. The device is invisible until you turn it on.', 'btMsg');
  };
  ['discSeconds', 'offOnDisc'].forEach(function (id) { $(id).addEventListener('input', function () { settingsDirty = true; }); $(id).addEventListener('change', function () { settingsDirty = true; }); });
  $('discSave').onclick = function () {
    var root = $('view-computers');
    root.querySelector('[data-err="discoverySeconds"]').textContent = '';
    say('', '', 'discMsg');
    var raw = $('discSeconds').value.trim();
    if (!/^\d+$/.test(raw)) { root.querySelector('[data-err="discoverySeconds"]').textContent = 'Enter a whole number of seconds from 30 to 600.'; return; }
    App.call('PUT', '/api/bluetooth/settings', { discoverySeconds: Number(raw), offWhenComputerDisconnects: $('offOnDisc').checked }).then(function (res) {
      if (res.status === 200) { settingsDirty = false; settingsShown = ''; say('Saved.', 'good', 'discMsg'); App.poll(); return; }
      var d = res.data;
      if (!d.field || !App.showError(root, d.field, d.message)) say(tr(d.message) || 'Could not save.', 'bad', 'discMsg');
    }).catch(function () {});
  };
  App.confirmInline($('forgetAll'), 'Confirm: forget all', function () {
    App.call('DELETE', '/api/bluetooth/peers').then(function (res) {
      reply(res, 'All computers forgotten. Discovery stays closed: start it to add one.', 'peersMsg');
      load(); App.poll();
    }).catch(function () {});
  });
})();
