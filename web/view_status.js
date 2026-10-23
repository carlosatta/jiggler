// Stato: the connection hero (which computer, since when, disconnect/pair) and the jiggler controls.
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr;
  var fmtDuration = JigLib.fmtDuration, fmtAgo = JigLib.fmtAgo;
  var ACTIONS = {
    disconnect: { label: 'Disconnetti', primary: false, run: function () {
      return App.call('POST', '/api/bluetooth/disconnect').then(function (r) {
        return r.status === 200 ? ['Scollegato. Il dispositivo resta invisibile finché non premi Riconnetti.', 'good'] : fail(r);
      }); } },
    reconnect: { label: 'Riconnetti', primary: true, run: function () {
      return App.call('POST', '/api/bluetooth/reconnect').then(function (r) {
        return r.status === 200 ? ['Di nuovo visibile: il computer si ricollega da solo.', 'good'] : fail(r);
      }); } },
    pair: { label: 'Abbina un computer', primary: true, run: function () {
      return App.call('POST', '/api/bluetooth/pairing', { seconds: 120 }).then(function (r) {
        return r.status === 200 ? ['Abbinamento aperto per 2 minuti.', 'good'] : fail(r);
      }); } },
    closePairing: { label: 'Chiudi abbinamento', primary: false, run: function () {
      return App.call('DELETE', '/api/bluetooth/pairing').then(function (r) {
        return r.status === 200 ? ['Abbinamento chiuso.', 'good'] : fail(r);
      }); } }
  };
  function fail(r) { return [tr(r.data && r.data.message) || 'Operazione non riuscita.', 'bad']; }

  var shownActions = "";

  function adjusted(status, elapsed) {
    var bt = JSON.parse(JSON.stringify(status.bluetooth || { pairing: {}, peerCount: 0 }));
    if (bt.pairing && bt.pairing.remainingMs != null) bt.pairing.remainingMs = Math.max(0, bt.pairing.remainingMs - elapsed);
    if (bt.computer) bt.computer.connectedForMs += elapsed;
    return bt;
  }

  function renderHero(status, elapsed) {
    var v = JigLib.connectionView(adjusted(status, elapsed), status.device);
    $('hero').setAttribute('data-tone', v.tone);
    $('heroTitle').textContent = v.title;
    $('heroDetail').textContent = v.detail;
    var key = v.actions.join(',');
    if (key !== shownActions) {
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
        ? 'Disconnetti non spegne il dispositivo: lo scollega e lo nasconde finché non lo riconnetti.' : '';
    }
  }

  function renderMotion(s, elapsed) {
    $('state').textContent = { running: 'In esecuzione', paused: 'In pausa', stopped: 'Fermo' }[s.state] || s.state;
    var next = $('next');
    if (s.state === 'paused') next.textContent = 'Il movimento riprende con Avvia.';
    else if (s.state === 'stopped') next.textContent = 'Nessun movimento programmato.';
    else if (!s.mouseConnected && s.msToNextMove === 0) next.textContent = 'In attesa che un computer si colleghi.';
    else next.textContent = 'Prossimo movimento tra ' + fmtDuration(Math.max(0, s.msToNextMove - elapsed));
  }

  function renderFacts(s) {
    $('count').textContent = s.moveCount;
    if (s.lastMoveAt !== null) $('last').textContent = new Date(s.lastMoveAt * 1000).toLocaleTimeString('it-IT');
    else if (s.lastMoveAgoMs !== null) $('last').textContent = fmtAgo(s.lastMoveAgoMs);
    else $('last').textContent = 'mai';
    $('wifi').textContent = s.wifiConnected ? 'Collegato' : 'Non collegato';
    $('bStart').disabled = s.state === 'running';
    $('bPause').disabled = s.state !== 'running';
    $('bStop').disabled = s.state === 'stopped';
    $('bMove').disabled = !s.mouseConnected;
  }

  App.view('stato', {
    onStatus: function (s) { renderHero(s, 0); renderMotion(s, 0); renderFacts(s); },
    onTick: function (s, elapsed) { renderHero(s, elapsed); renderMotion(s, elapsed); }
  });

  function control(method, path, okText) {
    return App.call(method, path).then(function (res) {
      if (res.status >= 400) say(res.data.error === 'mouse_not_connected' ? 'Nessun computer collegato: abbinane uno dalla sezione Computer.' : 'Operazione non riuscita.', 'bad');
      else say(okText, 'good');
      App.poll();
    }).catch(function () {});
  }
  $('bStart').onclick = function () { control('POST', '/api/start', 'Avviato.'); };
  $('bPause').onclick = function () { control('POST', '/api/pause', 'In pausa.'); };
  $('bStop').onclick = function () { control('POST', '/api/stop', 'Fermato.'); };
  $('bMove').onclick = function () { control('POST', '/api/move', 'Movimento in corso.'); setTimeout(App.poll, 1500); };
})();
