// Shared plumbing: DOM helpers, authenticated fetch, status polling, restart recovery. Views register themselves
// with App.view(name, { load, onStatus, onTick }).
var App = (function () {
  'use strict';
  var KEY = 'jigglerToken';
  var tr = JigLib.tr;
  var token = localStorage.getItem(KEY) || '';
  var views = {}, current = '';
  var status = null, receivedAt = 0;
  var failures = 0, inFlight = false, restarting = false, offlineShown = false;

  function $(id) { return document.getElementById(id); }

  function el(tag, attrs, kids) {
    var e = document.createElement(tag);
    Object.keys(attrs || {}).forEach(function (k) {
      if (k === 'class') e.className = attrs[k];
      else if (k === 'text') e.textContent = attrs[k];
      else e.setAttribute(k, attrs[k]);
    });
    (kids || []).forEach(function (c) { e.appendChild(c); });
    return e;
  }

  var uid = 0;
  function labeled(text, input, errKey) {
    var id = 'f' + (++uid);
    input.id = id;
    var box = el('div', { class: 'field' }, [el('label', { 'for': id, text: text }), input]);
    var err = el('div', { class: 'err' });
    err.setAttribute('data-err', errKey);
    box.appendChild(err);
    return box;
  }
  function clearErrors(root) { root.querySelectorAll('[data-err]').forEach(function (e) { e.textContent = ''; }); }
  function showError(root, field, message) {
    var e = root.querySelector('[data-err="' + field + '"]');
    if (e) { e.textContent = tr(message); return true; }
    return false;
  }
  function say(text, kind, id) { var m = $(id || 'msg'); m.textContent = text; m.className = 'msg ' + (kind || ''); }

  var bannerTimer = 0;
  function banner(text, kind, autoHideMs) {
    var b = $('banner');
    clearTimeout(bannerTimer);
    b.textContent = text || '';
    b.className = 'banner ' + (kind || '');
    b.hidden = !text;
    if (text && autoHideMs) bannerTimer = setTimeout(function () { banner(''); }, autoHideMs);
  }

  function setToken(t) { token = t; if (t) localStorage.setItem(KEY, t); else localStorage.removeItem(KEY); }
  function getToken() { return token; }

  function showLogin(error) {
    $('loginErr').textContent = error || '';
    $('login').hidden = false;
    $('tokenInput').value = '';
    $('tokenInput').focus();
  }

  // body: object (sent as JSON) or string (sent as is). Resolves { status, data }; rejects when unreachable.
  function call(method, path, body) {
    var opts = { method: method, headers: { 'Authorization': 'Bearer ' + token } };
    if (body !== undefined) {
      opts.headers['Content-Type'] = 'application/json';
      opts.body = typeof body === 'string' ? body : JSON.stringify(body);
    }
    return fetch(path, opts).then(function (r) {
      if (r.status === 401) { setToken(''); showLogin('Invalid token.'); throw new Error('unauthorized'); }
      return r.json().catch(function () { return {}; }).then(function (data) {
        failures = 0;
        document.body.classList.remove('offline');
        if (offlineShown && !restarting) { banner(''); offlineShown = false; }
        return { status: r.status, data: data };
      });
    }, function (e) {
      failures++;
      if (!restarting && failures >= 2) {
        document.body.classList.add('offline');
        banner('Device not reachable. Retrying.', 'bad');
        offlineShown = true;
      }
      throw e;
    });
  }

  // Two-step button: the first click arms it for 4 s ("Conferma: …"), the second runs the action.
  function confirmInline(btn, armedText, action) {
    var original = btn.textContent, armed = false, timer = 0;
    function reset() { armed = false; clearTimeout(timer); btn.textContent = original; btn.classList.remove('confirm'); }
    btn.addEventListener('click', function () {
      if (!armed) {
        armed = true;
        btn.textContent = armedText;
        btn.classList.add('confirm');
        timer = setTimeout(reset, 4000);
        return;
      }
      reset();
      action();
    });
    btn.isArmed = function () { return armed; };
  }

  // ---- views and routing ------------------------------------------------------------------
  function view(name, impl) { views[name] = impl; }
  function reloadCurrent() { if (views[current] && views[current].load && token) views[current].load(); }

  function route() {
    var name = (location.hash || '#status').slice(1);
    if (name === 'movement') name = 'device';  // the section was renamed
    if (!views[name]) name = 'status';
    var changed = name !== current;
    current = name;
    document.querySelectorAll('.view').forEach(function (v) { v.hidden = v.id !== 'view-' + name; });
    document.querySelectorAll('#nav a').forEach(function (a) {
      if (a.getAttribute('data-view') === name) a.setAttribute('aria-current', 'page');
      else a.removeAttribute('aria-current');
    });
    if (changed) {
      var h = document.querySelector('#view-' + name + ' h1');
      if (h && document.documentElement.getAttribute('data-ready')) { h.tabIndex = -1; h.focus({ preventScroll: true }); }
      window.scrollTo(0, 0);
    }
    reloadCurrent();
  }

  // ---- status polling ---------------------------------------------------------------------
  // While the device installs new firmware it refuses everything but the progress (503 "installing"), because
  // building an answer can take the memory the download needs. The dashboard then stops asking for the status and
  // looks at the progress every few seconds instead, which costs the device almost nothing.
  var installing = false, lastProbeAt = 0;
  var kInstallProbeMs = 5000;

  function enterInstalling() {
    if (installing) return;
    installing = true;
    lastProbeAt = 0;
    banner('Installing new firmware. The dashboard waits: the device restarts when it is done.', '');
  }

  function probeInstall() {
    if (performance.now() - lastProbeAt < kInstallProbeMs) return;
    lastProbeAt = performance.now();
    inFlight = true;
    call('GET', '/api/updates').then(function (res) {
      if (res.status === 503) return;
      if (res.data && res.data.state === 'restarting') { installing = false; waitForDevice(); return; }
      if (res.status === 200 && res.data && res.data.state !== 'installing') {
        installing = false;   // it ended without a restart: a failure the device survived
        banner('');
        reloadCurrent();
      }
    }).catch(function () {}).then(function () { inFlight = false; });
  }

  function poll() {
    if (!token || inFlight || restarting || document.hidden) return;
    if (installing) { probeInstall(); return; }
    inFlight = true;
    call('GET', '/api/status').then(function (res) {
      if (res.status === 503 && res.data && res.data.error === 'installing') { enterInstalling(); return; }
      status = res.data;
      receivedAt = performance.now();
      Object.keys(views).forEach(function (n) { if (views[n].onStatus) views[n].onStatus(status); });
    }).catch(function () {}).then(function () { inFlight = false; });
  }

  function tick() {
    if (!status) return;
    Object.keys(views).forEach(function (n) { if (views[n].onTick) views[n].onTick(status, performance.now() - receivedAt); });
  }

  // ---- restart ----------------------------------------------------------------------------
  function waitForDevice() {
    restarting = true;
    banner('Restarting… the page reconnects by itself.', '');
    var started = performance.now();
    function probe() {
      fetch('/api/status', { headers: { 'Authorization': 'Bearer ' + token } }).then(function (r) {
        if (r.ok) {
          restarting = false;
          installing = false;
          failures = 0;
          document.body.classList.remove('offline');
          banner('Device is back online.', 'good', 4000);
          reloadCurrent();
          poll();
        } else { again(); }
      }, again);
    }
    function again() {
      if (performance.now() - started > 60000) {
        restarting = false;
        banner('The device is not responding. Check its power and your network, then reload the page.', 'bad');
        return;
      }
      setTimeout(probe, 1500);
    }
    setTimeout(probe, 3000);
  }

  function restartDevice() {
    return call('POST', '/api/restart').then(function (res) {
      if (res.status === 200 || res.status === 202) waitForDevice();
    }).catch(function () {});
  }

  return {
    $: $, el: el, labeled: labeled, clearErrors: clearErrors, showError: showError, say: say, banner: banner,
    call: call, setToken: setToken, getToken: getToken, showLogin: showLogin, confirmInline: confirmInline,
    view: view, route: route, poll: poll, tick: tick, installing: function () { return installing; }, restartDevice: restartDevice, waitForDevice: waitForDevice, reloadCurrent: reloadCurrent,
    status: function () { return status; }
  };
})();
