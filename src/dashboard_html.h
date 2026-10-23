#pragma once

inline constexpr char kDashboardHtml[] = R"HTML(<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jiggler</title>
<link rel="icon" href="data:,">
<style>
:root{--bg:#f4f5f7;--card:#fff;--text:#15171c;--muted:#667085;--line:#e2e5ea;--accent:#2563eb;--on-accent:#fff;--ok:#15803d;--bad:#b91c1c;--warn:#b45309}
@media (prefers-color-scheme:dark){:root{--bg:#0e1014;--card:#171a21;--text:#e8eaee;--muted:#98a2b3;--line:#272c36;--accent:#6b9bff;--on-accent:#0e1014;--ok:#4ade80;--bad:#f87171;--warn:#fbbf24}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:560px;margin:0 auto;padding:16px}
h1{font-size:1.25rem;margin:8px 0 16px}
h2{font-size:.8rem;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);margin:0 0 12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:12px}
.chips{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px}
.chip{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border:1px solid var(--line);border-radius:999px;background:var(--card);font-size:.9rem}
.dot{width:9px;height:9px;border-radius:50%;background:var(--bad)}
.chip.on .dot{background:var(--ok)}
.state{font-size:1.6rem;font-weight:650}
.sub{color:var(--muted);margin-top:2px}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:14px}
.stats b{display:block;font-size:1.1rem;font-weight:600}
.stats span{color:var(--muted);font-size:.85rem}
.btns{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
button{font:inherit;min-height:44px;padding:8px 12px;border-radius:10px;border:1px solid var(--line);background:var(--card);color:var(--text);cursor:pointer}
button:disabled{opacity:.45;cursor:not-allowed}
button.primary{background:var(--accent);color:var(--on-accent);border-color:transparent}
button:focus-visible,input:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.btns .wide{grid-column:1/-1}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.field{margin-bottom:12px}
label{display:block;font-size:.9rem;margin-bottom:4px}
input{width:100%;font:inherit;min-height:44px;padding:8px 10px;border-radius:10px;border:1px solid var(--line);background:var(--bg);color:var(--text)}
.err{color:var(--bad);font-size:.85rem;min-height:1.1em;margin-top:2px}
.hint{color:var(--muted);font-size:.8rem}
#msg,#cfgMsg{min-height:1.4em;margin:4px 0 0}
#msg.bad,#cfgMsg.bad{color:var(--bad)}#msg.good,#cfgMsg.good{color:var(--ok)}
#banner{display:none;background:var(--bad);color:#fff;padding:8px 12px;border-radius:10px;margin-bottom:12px}
#login{position:fixed;inset:0;display:none;align-items:center;justify-content:center;background:rgba(0,0,0,.5);padding:16px}
#login .card{width:100%;max-width:360px;margin:0}
</style>
</head>
<body>
<main>
<h1>Jiggler</h1>
<div id="banner" role="alert"></div>
<div class="chips">
<span class="chip" id="chipMouse"><span class="dot"></span><span id="txtMouse">Mouse</span></span>
<span class="chip" id="chipWifi"><span class="dot"></span><span id="txtWifi">WiFi</span></span>
</div>

<section class="card">
<h2>Stato</h2>
<div class="state" id="state">&mdash;</div>
<div class="sub" id="next">&nbsp;</div>
<div class="stats">
<div><b id="count">0</b><span>Movimenti eseguiti</span></div>
<div><b id="last">&mdash;</b><span>Ultimo movimento</span></div>
</div>
</section>

<section class="card">
<h2>Controlli</h2>
<div class="btns">
<button id="bStart" class="primary">Avvia</button>
<button id="bPause">Pausa</button>
<button id="bStop">Ferma</button>
<button id="bMove" class="wide">Muovi ora</button>
</div>
<p id="msg" role="status" aria-live="polite"></p>
</section>

<section class="card">
<h2>Configurazione</h2>
<form id="cfg" novalidate>
<div class="row">
<div class="field"><label for="minIntervalSec">Intervallo minimo (s)</label><input id="minIntervalSec" inputmode="numeric" autocomplete="off"><div class="err" id="e_minIntervalSec"></div></div>
<div class="field"><label for="maxIntervalSec">Intervallo massimo (s)</label><input id="maxIntervalSec" inputmode="numeric" autocomplete="off"><div class="err" id="e_maxIntervalSec"></div></div>
</div>
<div class="field"><label for="amplitudePx">Ampiezza movimento (px)</label><input id="amplitudePx" inputmode="numeric" autocomplete="off"><div class="err" id="e_amplitudePx"></div></div>
<div class="row">
<div class="field"><label for="clickProbability">Click casuali (%)</label><input id="clickProbability" inputmode="decimal" autocomplete="off"><div class="err" id="e_clickProbability"></div></div>
<div class="field"><label for="scrollProbability">Scroll casuali (%)</label><input id="scrollProbability" inputmode="decimal" autocomplete="off"><div class="err" id="e_scrollProbability"></div></div>
</div>
<p class="hint">Intervalli 5&ndash;3600 s, ampiezza 5&ndash;600 px. Un click casuale pu&ograve; attivare ci&ograve; che si trova sotto il cursore: lascia 0% se non serve.</p>
<button class="primary" type="submit">Salva</button>
<p id="cfgMsg" role="status" aria-live="polite"></p>
</form>
</section>
</main>

<div id="login" role="dialog" aria-modal="true" aria-labelledby="loginTitle">
<form class="card" id="loginForm">
<h2 id="loginTitle">Accesso</h2>
<div class="field"><label for="tokenInput">Token API</label><input id="tokenInput" type="password" autocomplete="off"><div class="err" id="loginErr"></div></div>
<button class="primary" type="submit">Entra</button>
</form>
</div>

<script>
(function () {
  'use strict';
  var KEY = 'jigglerToken';
  var token = localStorage.getItem(KEY) || '';
  var status = null, receivedAt = 0, dirty = {};
  var FIELDS = [
    { id: 'minIntervalSec', scale: 1 }, { id: 'maxIntervalSec', scale: 1 }, { id: 'amplitudePx', scale: 1 },
    { id: 'clickProbability', scale: 100 }, { id: 'scrollProbability', scale: 100 }
  ];
  var MESSAGES = {
    'must be between 5 and 3600': 'Deve essere tra 5 e 3600.',
    'must be between 5 and 600': 'Deve essere tra 5 e 600.',
    'must be between 0 and 1': 'Deve essere tra 0 e 100.',
    'maxIntervalSec must be >= minIntervalSec': 'Il massimo non può essere minore del minimo.',
    'must be an integer': 'Inserisci un numero intero.',
    'must be a number': 'Inserisci un numero.'
  };
  var $ = function (id) { return document.getElementById(id); };

  function banner(text) { var b = $('banner'); b.textContent = text; b.style.display = text ? 'block' : 'none'; }
  function say(text, kind, id) { var m = $(id || 'msg'); m.textContent = text; m.className = kind || ''; }
  function setToken(t) { token = t; if (t) localStorage.setItem(KEY, t); else localStorage.removeItem(KEY); }
  function showLogin(error) {
    $('loginErr').textContent = error || '';
    $('login').style.display = 'flex';
    $('tokenInput').value = '';
    $('tokenInput').focus();
  }

  function call(method, path, body) {
    var opts = { method: method, headers: { 'Authorization': 'Bearer ' + token } };
    if (body !== undefined) { opts.headers['Content-Type'] = 'application/json'; opts.body = JSON.stringify(body); }
    return fetch(path, opts).then(function (r) {
      if (r.status === 401) { setToken(''); showLogin('Token non valido.'); throw new Error('unauthorized'); }
      return r.json().catch(function () { return {}; }).then(function (data) {
        banner('');
        return { status: r.status, data: data };
      });
    }, function (e) { banner('Dispositivo non raggiungibile.'); throw e; });
  }

  function fmtDuration(ms) {
    var s = Math.ceil(ms / 1000), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
    var pad = function (n) { return (n < 10 ? '0' : '') + n; };
    return h > 0 ? h + ':' + pad(m) + ':' + pad(sec) : m + ':' + pad(sec);
  }
  function fmtAgo(ms) {
    var m = Math.floor(ms / 60000);
    if (m < 1) return 'meno di un minuto fa';
    return m === 1 ? '1 minuto fa' : m + ' minuti fa';
  }

  function setChip(id, textId, on, label) {
    $(id).className = 'chip' + (on ? ' on' : '');
    $(textId).textContent = label + (on ? ': connesso' : ': non connesso');
  }

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
      if (d.field && $('e_' + d.field)) fieldError(d.field, MESSAGES[d.message] || d.message || 'Valore non valido.');
      else say('Salvataggio non riuscito.', 'bad', 'cfgMsg');
    }).catch(function () {});
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
</script>
</body>
</html>
)HTML";
