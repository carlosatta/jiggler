// Pure helpers (no DOM): inlined before app.js in the page and loaded as a module by the node tests.
var JigLib = (function () {
  'use strict';
  var MESSAGES = {
    'must be between 5 and 3600': 'Deve essere tra 5 e 3600.',
    'must be between 5 and 600': 'Deve essere tra 5 e 600.',
    'must be between 0 and 1': 'Deve essere tra 0 e 100.',
    'maxIntervalSec must be >= minIntervalSec': 'Il massimo non può essere minore del minimo.',
    'must be >= minIntervalSec': 'Il massimo non può essere minore del minimo.',
    'must be an integer': 'Inserisci un numero intero.',
    'must be a number': 'Inserisci un numero.',
    'must look like AA:BB:CC:DD:EE:FF': 'Formato AA:BB:CC:DD:EE:FF.',
    'must be a unicast address (first byte even)': 'Indirizzo unicast: il primo byte deve essere pari.',
    'must be 1 to 29 characters': 'Da 1 a 29 caratteri.',
    'must be 1 to 32 characters': 'Da 1 a 32 caratteri.',
    'unknown profile': 'Profilo sconosciuto.',
    'profile is for a different device kind': 'Profilo di un altro tipo di dispositivo.',
    'at least one device must be enabled': 'Abilita almeno un dispositivo.',
    'duplicate id': 'Id duplicato.',
    'must be an integer from 0 to 65535': 'Intero da 0 a 65535.',
    'unknown script': 'Script inesistente.',
    'must be between 5 and 86400': 'Tra 5 e 86400 secondi.',
    'must be empty or 8 to 63 printable ASCII characters': 'Vuota (generata) oppure 8-63 caratteri ASCII.',
    'the built-in script cannot be changed': 'Lo script predefinito non si può modificare.',
    'the built-in script cannot be removed': 'Lo script predefinito non si può eliminare.',
    'at most 8 user scripts': 'Massimo 8 script utente.',
    'at most 4 automations': 'Massimo 4 automazioni.',
    'must be 1 to 24 characters of a-z 0-9 _ -': 'Da 1 a 24 caratteri, solo lettere minuscole, cifre, trattino e trattino basso.',
    'must be auto or ap': 'Scegli Auto o Rete propria.',
    'at least one mouse must be enabled (the jiggler needs it)': 'Abilita almeno un mouse: il jiggler ne ha bisogno.',
    'could not save the devices (storage full?)': 'Memoria piena: impossibile salvare.',
    'could not save the script (storage full?)': 'Memoria piena: impossibile salvare lo script.',
    'could not save the automations (storage full?)': 'Memoria piena: impossibile salvare.',
    'script is too complex (max 10000 operations)': 'Script troppo complesso (massimo 10000 operazioni).',
    'script is too large (max 3000 bytes)': 'Script troppo grande.',
    'the pairing window must last between 30 and 600 seconds': 'L’abbinamento deve durare tra 30 e 600 secondi.',
    'no such paired computer': 'Questo computer non è più abbinato.',
    'the name must be 1 to 32 characters': 'Il nome deve avere da 1 a 32 caratteri.',
    'the name must not contain control characters': 'Il nome non può contenere caratteri di controllo.',
    'the current token is not correct': 'Il token attuale non è corretto.',
    'the current token is required': 'Inserisci il token attuale.',
    'must be 8 to 40 printable characters without spaces': 'Da 8 a 40 caratteri, senza spazi.',
    'could not save the token (storage full?)': 'Memoria piena: token non salvato.'
  };

  function tr(message) { return MESSAGES[message] || message || 'Valore non valido.'; }

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

  function plural(n, one, many) { return n + ' ' + (n === 1 ? one : many); }

  // A duration, as in "da 12 minuti" / "da 1 ora e 5 min".
  function fmtSince(ms) {
    var m = Math.floor(ms / 60000);
    if (m < 1) return 'meno di un minuto';
    if (m < 60) return plural(m, 'minuto', 'minuti');
    var h = Math.floor(m / 60), rest = m % 60;
    return plural(h, 'ora', 'ore') + (rest ? ' e ' + rest + ' min' : '');
  }

  // What the Stato page says about the Bluetooth link, from /api/status. Pure so the wording is testable.
  function connectionView(bt, device) {
    var name = (device && device.bleName) || 'il dispositivo';
    var pairing = bt.pairing || {};
    var timed = pairing.open && pairing.remainingMs !== null && pairing.remainingMs !== undefined;
    var window_ = timed ? ' Abbinamento aperto, scade tra ' + fmtDuration(pairing.remainingMs) + '.' : '';
    if (bt.connected && bt.computer) {
      var actions = ['disconnect'];
      if (timed) actions.push('closePairing');
      return { tone: 'ok', title: 'Collegato a ' + bt.computer.label,
        detail: 'da ' + fmtSince(bt.computer.connectedForMs) + ' · ' + name + '.' + window_, actions: actions };
    }
    if (bt.held) {
      return { tone: 'warn', title: 'Scollegato',
        detail: 'Il dispositivo è invisibile: nessun computer può collegarsi finché non lo riconnetti.', actions: ['reconnect'] };
    }
    if (pairing.open && !timed) {
      return { tone: 'open', title: 'Pronto per l’abbinamento',
        detail: 'Sul computer apri le impostazioni Bluetooth e scegli «' + name + '».', actions: [] };
    }
    if (timed) {
      return { tone: 'open', title: 'Abbinamento aperto',
        detail: 'Scade tra ' + fmtDuration(pairing.remainingMs) + '. Sul computer scegli «' + name + '».', actions: ['closePairing'] };
    }
    return { tone: 'idle', title: 'Nessun computer collegato',
      detail: 'Si ricollega da solo quando il computer abbinato è acceso e vicino.', actions: ['pair'] };
  }

  function checkNewToken(next, confirm) {
    if (next.length < 8 || next.length > 40 || /\s/.test(next)) return 'Da 8 a 40 caratteri, senza spazi.';
    if (next !== confirm) return 'I due token non coincidono.';
    return '';
  }

  function hex4(n) { return '0x' + ('0000' + n.toString(16).toUpperCase()).slice(-4); }

  return { MESSAGES: MESSAGES, tr: tr, fmtDuration: fmtDuration, fmtAgo: fmtAgo, fmtSince: fmtSince,
    connectionView: connectionView, checkNewToken: checkNewToken, hex4: hex4 };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = JigLib;
