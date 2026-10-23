// Boot: login, routing, polling.
(function () {
  'use strict';
  var $ = App.$;

  $('tokenShow').addEventListener('change', function () { $('tokenInput').type = $('tokenShow').checked ? 'text' : 'password'; });
  $('loginForm').addEventListener('submit', function (ev) {
    ev.preventDefault();
    var t = $('tokenInput').value.trim();
    if (!t) { $('loginErr').textContent = 'Enter the token.'; return; }
    App.setToken(t);
    $('login').hidden = true;
    App.poll();
    App.reloadCurrent();
  });

  window.addEventListener('hashchange', App.route);
  document.addEventListener('visibilitychange', function () { if (!document.hidden) App.poll(); });

  App.route();
  document.documentElement.setAttribute('data-ready', '1');
  if (!App.getToken()) App.showLogin('');
  App.poll();
  setInterval(App.poll, 1000);
  setInterval(App.tick, 250);
})();
