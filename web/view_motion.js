// Movement: the random-movement configuration.
(function () {
  'use strict';
  var $ = App.$, el = App.el, say = App.say, tr = JigLib.tr;
  var labeled = App.labeled, clearErrors = App.clearErrors, showError = App.showError;
  var FIELDS = [
    { id: 'minIntervalSec', scale: 1 }, { id: 'maxIntervalSec', scale: 1 }, { id: 'amplitudePx', scale: 1 },
    { id: 'clickProbability', scale: 100 }, { id: 'scrollProbability', scale: 100 }
  ];
  var dirty = {}, config = null;

  function fieldError(id, text) { var e = $('e_' + id); if (e) e.textContent = text; }

  function populate(s) {
    config = s.config;
    FIELDS.forEach(function (f) {
      if (dirty[f.id] || document.activeElement === $(f.id)) return;
      $(f.id).value = String(Math.round(s.config[f.id] * f.scale));
    });
  }
  FIELDS.forEach(function (f) { $(f.id).addEventListener('input', function () { dirty[f.id] = true; }); });

  $('cfg').addEventListener('submit', function (ev) {
    ev.preventDefault();
    FIELDS.forEach(function (f) { fieldError(f.id, ''); $(f.id).removeAttribute('aria-invalid'); });
    if (!config) return;
    var body = {}, invalid = false;
    FIELDS.forEach(function (f) {
      var raw = $(f.id).value.trim().replace(',', '.');
      if (raw === '') { fieldError(f.id, 'This field is required.'); invalid = true; return; }
      var n = Number(raw);
      if (!isFinite(n)) { fieldError(f.id, 'Enter a number.'); invalid = true; return; }
      if (n !== Math.round(config[f.id] * f.scale)) body[f.id] = n / f.scale;
    });
    if (invalid) return;
    say('', '', 'cfgMsg');
    if (Object.keys(body).length === 0) { say('Nothing to save.', '', 'cfgMsg'); return; }
    App.call('PUT', '/api/config', body).then(function (res) {
      if (res.status === 200) { dirty = {}; say('Configuration saved.', 'good', 'cfgMsg'); App.poll(); return; }
      var d = res.data;
      if (d.field && $('e_' + d.field)) { fieldError(d.field, tr(d.message)); $(d.field).setAttribute('aria-invalid', 'true'); }
      else say('Could not save.', 'bad', 'cfgMsg');
    }).catch(function () {});
  });

  App.view('movement', {
    load: function () { if (App.status()) populate(App.status()); },
    onStatus: function (s) { populate(s); }
  });
})();
