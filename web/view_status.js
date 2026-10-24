// Status: the connection hero (which computer, since when, disconnect/pair) and the run controls.
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr;
  var fmtDuration = JigLib.fmtDuration, fmtAgo = JigLib.fmtAgo;
  var CHOICE_KEY = 'jigglerRunChoice';

  function fail(r) { return [tr(r.data && r.data.message) || 'That did not work.', 'bad']; }
  var ACTIONS = {
    disconnect: { label: 'Disconnect', primary: false, run: function () {
      return App.call('POST', '/api/bluetooth/disconnect').then(function (r) {
        return r.status === 200 ? ['Disconnected. The device stays hidden until you press Reconnect.', 'good'] : fail(r);
      }); } },
    reconnect: { label: 'Reconnect', primary: true, run: function () {
      return App.call('POST', '/api/bluetooth/reconnect').then(function (r) {
        return r.status === 200 ? ['Visible again: the computer reconnects by itself.', 'good'] : fail(r);
      }); } },
    pair: { label: 'Pair a computer', primary: true, run: function () {
      return App.call('POST', '/api/bluetooth/pairing').then(function (r) {
        return r.status === 200 ? ['Pairing is open for 3 minutes.', 'good'] : fail(r);
      }); } },
    closePairing: { label: 'Stop pairing', primary: false, run: function () {
      return App.call('DELETE', '/api/bluetooth/pairing').then(function (r) {
        return r.status === 200 ? ['Pairing stopped.', 'good'] : fail(r);
      }); } }
  };

  var shownActions = '';

  function adjusted(status, elapsed) {
    var bt = JSON.parse(JSON.stringify(status.bluetooth || { pairing: {}, peerCount: 0 }));
    if (bt.pairing && bt.pairing.remainingMs != null) bt.pairing.remainingMs = Math.max(0, bt.pairing.remainingMs - elapsed);
    if (bt.heldRemainingMs != null) bt.heldRemainingMs = Math.max(0, bt.heldRemainingMs - elapsed);
    if (bt.computer) bt.computer.connectedForMs += elapsed;
    return bt;
  }

  function renderHero(status, elapsed) {
    var v = JigLib.connectionView(adjusted(status, elapsed), status.device);
    $('hero').setAttribute('data-tone', v.tone);
    $('heroTitle').textContent = v.title;
    $('heroDetail').textContent = v.detail;
    var key = v.actions.join(',');
    if (key === shownActions) return;
    shownActions = key;
    var box = $('heroActions');
    box.textContent = '';
    v.actions.forEach(function (name) {
      var a = ACTIONS[name];
      var b = el('button', { text: a.label, class: a.primary ? 'primary' : '' });
      b.onclick = function () {
        b.disabled = true;
        say('', '', 'heroMsg');
        a.run().then(function (r) { say(r[0], r[1], 'heroMsg'); App.poll(); }).catch(function () {}).then(function () { b.disabled = false; });
      };
      box.appendChild(b);
    });
    $('heroNote').textContent = key.indexOf('disconnect') >= 0
      ? 'Disconnect does not power the device off: it drops the link and hides the device from every computer until you press Reconnect (a restart also brings it back).' : '';
  }

  function renderMotion(s, elapsed) {
    var running = s.state === 'running', paused = s.state === 'paused';
    $('state').textContent = running ? 'Running' : paused ? 'Paused' : 'Stopped';
    var next = $('next');
    if (paused) {
      var back = s.resumesInMs != null ? Math.max(0, s.resumesInMs - elapsed) : null;
      var text = back != null ? 'Resumes in ' + fmtDuration(back) + ' (at ' + JigLib.stopsAt(back, new Date()) + ')' : 'Paused until you press Resume';
      if (s.stopsInMs != null) {
        var left0 = Math.max(0, s.stopsInMs - elapsed);
        text += ' · stops at ' + JigLib.stopsAt(left0, new Date());
      }
      next.textContent = text + '.';
    } else if (!running) next.textContent = 'Not running. Press Start to keep the computer awake.';
    else {
      var parts = [];
      if (!s.mouseConnected && s.msToNextMove === 0) parts.push('Waiting for a computer to connect.');
      else parts.push('Next movement in ' + fmtDuration(Math.max(0, s.msToNextMove - elapsed)));
      if (s.stopsInMs != null) {
        var left = Math.max(0, s.stopsInMs - elapsed);
        parts.push('stops in ' + fmtDuration(left) + ' (at ' + JigLib.stopsAt(left, new Date()) + ')');
      }
      next.textContent = parts.join(' · ');
    }
  }

  function renderFacts(s) {
    $('count').textContent = s.moveCount;
    if (s.lastMoveAt !== null) $('last').textContent = new Date(s.lastMoveAt * 1000).toLocaleTimeString('en-GB');
    else if (s.lastMoveAgoMs !== null) $('last').textContent = fmtAgo(s.lastMoveAgoMs);
    else $('last').textContent = 'never';
    $('wifi').textContent = s.wifiConnected ? 'Connected' : 'Not connected';
    var sc = s.schedule;
    if (!sc) $('clock').textContent = '—';
    else if (!sc.timeSynced) $('clock').textContent = 'Not synced (needs internet)';
    else $('clock').textContent = sc.localTime + ' · ' + sc.timezone + (sc.active ? ' · schedule open' : '');
    var running = s.state === 'running', paused = s.state === 'paused', stopped = !running && !paused;
    $('bToggle').textContent = stopped ? 'Start' : 'Stop';
    $('bToggle').className = stopped ? 'primary' : '';
    $('bPause').hidden = !running;
    $('bResume').hidden = !paused;
    $('pauseFor').hidden = !running;
    $('runFor').hidden = !stopped;
    $('runErr').textContent = running ? '' : $('runErr').textContent;
    $('bMove').disabled = !s.mouseConnected;
  }

  // ---- first-time setup: shown while no computer is paired ----
  function renderSetup(status, elapsed) {
    var bt = status.bluetooth;
    var needed = !!bt && !bt.peerCount;
    $('setup').hidden = !needed;
    $('hero').hidden = needed;
    $('movementBlock').hidden = needed;
    if (!needed) return;
    var p = bt.pairing || {};
    var open = !!p.open && p.remainingMs != null;
    $('setupName').textContent = '“' + ((status.device && status.device.bleName) || 'the device') + '”';
    $('stepPair').className = 'step' + (open ? ' done' : ' active');
    $('stepPair').querySelector('.num').textContent = open ? '✓' : '2';
    $('stepChoose').className = 'step' + (open ? ' active' : '');
    $('setupWait').hidden = !open;
    $('setupOpen').hidden = open;
    $('setupStop').hidden = !open;
    $('setupPairText').textContent = open
      ? 'Pairing is open: closes in ' + fmtDuration(Math.max(0, p.remainingMs - elapsed)) + '. It closes by itself as soon as a computer pairs.'
      : 'Pairing is closed, so nobody can connect by accident. Open it for 3 minutes. You can also tap the BOOT button once on the board.';
  }
  $('setupOpen').onclick = function () {
    $('setupOpen').disabled = true;
    ACTIONS.pair.run().then(function (r) { say(r[1] === 'bad' ? r[0] : '', r[1], 'setupMsg'); App.poll(); }).catch(function () {}).then(function () { $('setupOpen').disabled = false; });
  };
  $('setupStop').onclick = function () {
    ACTIONS.closePairing.run().then(function (r) { say(r[1] === 'bad' ? r[0] : '', r[1], 'setupMsg'); App.poll(); }).catch(function () {});
  };

  App.view('status', {
    onStatus: function (s) { renderSetup(s, 0); renderHero(s, 0); renderMotion(s, 0); renderFacts(s); },
    onTick: function (s, elapsed) { renderSetup(s, elapsed); renderHero(s, elapsed); renderMotion(s, elapsed); }
  });

  // ---- run for ----
  function readChoice() {
    return { choice: $('runChoice').value, time: $('runUntil').value, custom: $('runCustom').value };
  }
  function showChoiceFields() {
    var c = $('runChoice').value;
    $('runUntilBox').hidden = c !== 'until';
    $('runCustomBox').hidden = c !== 'custom';
    $('runErr').textContent = '';
  }
  $('runChoice').addEventListener('change', function () {
    showChoiceFields();
    try { localStorage.setItem(CHOICE_KEY, JSON.stringify(readChoice())); } catch (e) {}
  });
  ['runUntil', 'runCustom'].forEach(function (id) {
    $(id).addEventListener('input', function () {
      $('runErr').textContent = '';
      try { localStorage.setItem(CHOICE_KEY, JSON.stringify(readChoice())); } catch (e) {}
    });
  });
  (function restoreChoice() {
    try {
      var saved = JSON.parse(localStorage.getItem(CHOICE_KEY) || 'null');
      if (saved && $('runChoice').querySelector('option[value="' + saved.choice + '"]')) {
        $('runChoice').value = saved.choice;
        $('runUntil').value = saved.time || '';
        $('runCustom').value = saved.custom || '';
      }
    } catch (e) {}
    showChoiceFields();
  })();

  function control(method, path, body, okText) {
    return App.call(method, path, body).then(function (res) {
      if (res.status >= 400) say(res.data.error === 'mouse_not_connected' ? 'No computer connected: pair one from the Computers section.' : tr(res.data.message) || 'That did not work.', 'bad');
      else say(okText, 'good');
      App.poll();
    }).catch(function () {});
  }

  $('bPause').onclick = function () {
    var minutes = Number($('pauseChoice').value);
    try { localStorage.setItem('jigglerPauseChoice', String(minutes)); } catch (e) {}
    if (minutes > 0) control('POST', '/api/pause', { minutes: minutes }, 'Paused: it resumes by itself in ' + JigLib.fmtSince(minutes * 60000) + '.');
    else control('POST', '/api/pause', undefined, 'Paused until you press Resume.');
  };
  $('bResume').onclick = function () { control('POST', '/api/resume', undefined, 'Resumed.'); };
  (function restorePauseChoice() {
    try {
      var saved = localStorage.getItem('jigglerPauseChoice');
      if (saved !== null && $('pauseChoice').querySelector('option[value="' + saved + '"]')) $('pauseChoice').value = saved;
    } catch (e) {}
  })();

  $('bToggle').onclick = function () {
    var s = App.status();
    var active = s && (s.state === 'running' || s.state === 'paused');
    if (active) { control('POST', '/api/stop', undefined, 'Stopped.'); return; }
    var minutes = JigLib.runForMinutes(readChoice(), new Date());
    if (minutes === null) {
      $('runErr').textContent = $('runChoice').value === 'until' ? 'Pick the time it should stop.' : 'Enter a whole number of minutes from 1 to 1440.';
      return;
    }
    $('runErr').textContent = '';
    if (minutes === 0) control('POST', '/api/start', undefined, 'Started.');
    else control('POST', '/api/start', { minutes: minutes }, 'Started: it stops by itself in ' + JigLib.fmtSince(minutes * 60000) + '.');
  };
  $('bMove').onclick = function () { control('POST', '/api/move', undefined, 'Moving now.'); setTimeout(App.poll, 1500); };
})();
