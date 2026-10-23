// Computer: pairing window and the list of paired computers (rename, forget).
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr;
  var peers = [], shown = '', editing = null, lastFetch = 0, busy = false;

  function reply(res, okText, id) {
    if (res.status === 200) { say(okText, 'good', id); return true; }
    say(tr(res.data && res.data.message) || 'Operazione non riuscita.', 'bad', id);
    return false;
  }

  function renderPairing(status, elapsed) {
    var bt = status && status.bluetooth;
    if (!bt) return;
    var p = bt.pairing || {};
    var timed = p.open && p.remainingMs != null;
    var firstRun = bt.peerCount === 0;
    if (timed) $('pairInfo').textContent = 'Abbinamento aperto: scade tra ' + JigLib.fmtDuration(Math.max(0, p.remainingMs - elapsed)) + '.';
    else if (p.open) $('pairInfo').textContent = 'Nessun computer abbinato: l’abbinamento resta aperto finché non ne colleghi uno.';
    else $('pairInfo').textContent = 'Abbinamento chiuso: nessun nuovo computer può collegarsi.';
    $('pairOpen').disabled = busy || (p.open && !timed);
    $('pairOpen').textContent = timed ? 'Riapri (' + ($('pairDur').selectedOptions[0].text) + ')' : 'Apri abbinamento';
    $('pairClose').disabled = busy || !timed;
    $('pairDur').disabled = firstRun && p.open;
  }

  function peerRow(p) {
    var li = el('li');
    if (editing === p.id) {
      var input = el('input', { autocomplete: 'off', 'aria-label': 'Nome del computer', maxlength: '32' });
      input.value = p.label;
      var save = el('button', { class: 'primary', text: 'Salva' });
      var cancel = el('button', { text: 'Annulla' });
      var err = el('div', { class: 'err' });
      function done() { editing = null; shown = ''; draw(); }
      function commit() {
        var name = input.value.trim();
        if (!name) { err.textContent = 'Scrivi un nome.'; return; }
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
    if (p.connected) meta.appendChild(el('span', { class: 'peer-live', text: 'Collegato adesso' }));
    else meta.appendChild(document.createTextNode('Abbinato'));
    meta.appendChild(document.createTextNode(' · ' + p.address));
    var rename = el('button', { class: 'link', text: 'Rinomina' });
    rename.onclick = function () { editing = p.id; shown = ''; draw(); };
    var forget = el('button', { class: 'link danger', text: 'Dimentica' });
    App.confirmInline(forget, 'Conferma: dimentica', function () {
      App.call('DELETE', '/api/bluetooth/peers/' + p.id).then(function (res) {
        reply(res, '«' + p.label + '» dimenticato. Per usarlo di nuovo va abbinato daccapo.', 'peersMsg');
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

  App.view('computer', {
    load: load,
    onStatus: function (s) {
      renderPairing(s, 0);
      if (!$('view-computer').hidden && performance.now() - lastFetch > 2000) load();
    },
    onTick: function (s, elapsed) { renderPairing(s, elapsed); }
  });

  $('pairOpen').onclick = function () {
    busy = true; renderPairing(App.status(), 0);
    App.call('POST', '/api/bluetooth/pairing', { seconds: Number($('pairDur').value) }).then(function (res) {
      reply(res, 'Abbinamento aperto. Sul computer scegli il dispositivo dall’elenco Bluetooth.', 'pairMsg');
    }).catch(function () {}).then(function () { busy = false; App.poll(); });
  };
  $('pairClose').onclick = function () {
    busy = true; renderPairing(App.status(), 0);
    App.call('DELETE', '/api/bluetooth/pairing').then(function (res) {
      reply(res, 'Abbinamento chiuso.', 'pairMsg');
    }).catch(function () {}).then(function () { busy = false; App.poll(); });
  };
  App.confirmInline($('forgetAll'), 'Conferma: dimentica tutti', function () {
    App.call('DELETE', '/api/bluetooth/peers').then(function (res) {
      reply(res, 'Tutti i computer sono stati dimenticati. L’abbinamento è di nuovo aperto.', 'peersMsg');
      load(); App.poll();
    }).catch(function () {});
  });
})();
