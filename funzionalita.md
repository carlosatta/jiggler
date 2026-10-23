# Jiggler ESP32 — Funzionalità attese

Dispositivo basato su ESP32 che tiene sveglio un computer simulando un mouse
Bluetooth reale. Oltre al comportamento base, espone controllo e stato via WiFi
attraverso dashboard, API REST e un server MCP.

## 1. Mouse Bluetooth

- Si presenta al computer come un mouse Logitech MX Master (nome, produttore e
  identificativi del dispositivo coerenti con un modello reale).
- Indirizzo Bluetooth con prefisso Logitech, così che a un controllo non risulti
  un dispositivo Espressif.
- Per il sistema operativo è un normale mouse Bluetooth: nessun software o driver
  da installare sul computer da tenere sveglio.

## 2. Movimento

- Muove il cursore a intervalli casuali, non a cadenza fissa, per evitare pattern
  riconoscibili.
- Movimento di tipo "umano": traiettorie curve, accelerazione e decelerazione
  graduali, micro-oscillazioni. Niente scatti in linea retta a velocità costante.
- Il cursore torna in prossimità del punto di partenza, così non "deriva" sullo
  schermo col passare delle ore.
- Occasionali click o scroll casuali, per completare il profilo di attività.
- Intervallo minimo e massimo configurabili.

## 3. Connessione WiFi

- L'ESP32 si collega alla rete WiFi di casa per essere raggiungibile da
  dashboard, API e MCP.
- Se non riesce a connettersi, avvia una propria rete temporanea (access point)
  da cui inserire le credenziali WiFi la prima volta, senza doverle scrivere nel
  codice.
- Le credenziali restano salvate per i riavvii successivi.
- Il funzionamento del mouse Bluetooth resta indipendente dal WiFi: se la rete
  cade, il jiggler continua a lavorare.

## 4. Dashboard web

Pagina raggiungibile dal browser (sulla rete locale) per controllare il
dispositivo senza strumenti tecnici:

- Stato in tempo reale: mouse connesso o no, WiFi connesso o no, tempo al prossimo
  movimento.
- Avvio e pausa del jiggler.
- Modifica degli intervalli (minimo/massimo) e dell'ampiezza dei movimenti.
- Pulsante per forzare subito un movimento di prova.
- Contatore dei movimenti eseguiti e orario dell'ultimo.

## 5. API REST

Interfaccia per controllare il dispositivo da script, automazioni o altri
programmi:

- Lettura dello stato completo.
- Start / stop / pausa.
- Movimento immediato su richiesta.
- Lettura e modifica della configurazione (intervalli, ampiezza, comportamento).

## 6. Server MCP

Il dispositivo espone un server MCP, così da poter essere comandato direttamente
da un assistente come Claude:

- Chiedere lo stato ("il jiggler è attivo? quanto manca al prossimo movimento?").
- Avviare, mettere in pausa o fermare.
- Cambiare la configurazione a voce.
- Far muovere il mouse su richiesta.

## 7. Indicazioni fisiche

- LED di stato: indica se il mouse è connesso e segnala ogni movimento.
- Alimentazione via USB (presa a muro o porta USB del monitor).

## Note

- L'emulazione è di facciata: nome e identificativi corrispondono a un MX Master,
  ma il dispositivo non risponde al protocollo proprietario Logitech (HID++). Per
  il computer è un mouse Bluetooth a tutti gli effetti; software come Logi Options+
  non lo riconoscerebbero come MX Master reale.
- Su computer gestiti da un'azienda possono essere attivi strumenti che rilevano
  dispositivi nuovi o pattern di attività: vale la pena tenerne conto.
