# Spike: identità Bluetooth separate e simultanee sull'ESP32 classico

Data: 2026-10-23. Chip: ESP32-D0WDQ6 (BLE 4.2), NimBLE-Arduino 1.4.x, Arduino-ESP32 (platform `espressif32@6.x`). Controparte: un Mac con `bleak` (CoreBluetooth). Esperimento usa-e-getta, nessun codice entra nel prodotto.

## Domanda
Con un solo ESP32 classico si possono presentare **due identità BLE** (nome e indirizzo diversi) **contemporaneamente**, con un computer collegato a ciascuna?

## Come
`spike/identities/main.cpp`: un servizio GATT semplice (0xFFF0/0xFFF1, non HID perché CoreBluetooth nasconde l'HID alle app). Identità A: indirizzo pubblico del chip, nome `SpikeA`. Identità B: indirizzo *random static* `c2:00:00:00:de:c1`, nome `SpikeB`, impostato con `ble_hs_id_set_rnd`. Le due identità si alternano ogni 800 ms nell'advertising (un solo set di advertising sul controller BLE 4.2). Script `connect_one.py` e `connect_both.py` (bleak). Per rifarlo serve un env `[env:spike]` con `build_src_filter = -<*> +<../spike/identities/>`, `lib_deps = h2zero/NimBLE-Arduino@^1.4.3`.

## Risultati

| Prova | Esito |
|---|---|
| Scansione: due nomi con due indirizzi diversi | **Sì.** `SpikeA` e `SpikeB` compaiono con identificatori CoreBluetooth distinti; la B è stabile (c2:00:00:00:de:c1). |
| Connessione alla sola A | **Sì**, tenuta, lettura GATT ok. |
| Connessione alla sola B (random static) | **Sì**, tenuta, lettura GATT ok. |
| Advertising mentre A è collegata | **Sì**: A e B restano visibili (A 17, B 14 annunci in 8 s). |
| Connessione a B **mentre A è collegata** | **No**: timeout a 25 s; il dispositivo non vede nessun evento di connessione (solo quello di A). Stesso esito anche annunciando solo B in continuo (22 annunci), quindi non è un problema di alternanza. |

Dettaglio utile: chiamare `NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)` *dopo* `ble_hs_id_set_rnd` rigenera l'indirizzo (nella prima prova la B appariva con 5 identificatori diversi); l'ordine giusto è tipo di indirizzo e poi `ble_hs_id_set_rnd`.

## Cosa NON è stato stabilito
Perché la seconda connessione fallisce: limite del controller dell'ESP32 (indirizzo proprio diverso con una connessione già attiva) oppure del Mac (CoreBluetooth). Non ho isolato la causa; l'esperimento si è fermato qui per tempo. Non ho provato un secondo computer né un telefono come seconda centrale.

## Limiti strutturali, indipendenti dall'esito
1. **Un solo database GATT.** Tutte le identità condividono servizi, PnP ID (VID/PID), produttore e descrittore HID. Per renderle diverse per davvero servirebbe restituire valori diversi a seconda dell'indirizzo con cui è arrivata la connessione (callback di lettura con `our_ota_addr`), cosa fragile perché gli host mettono in cache il GATT.
2. **Il prefisso di un vendor reale è possibile solo sull'indirizzo pubblico.** Un indirizzo random static deve avere i due bit alti a 1 (primo byte ≥ 0xC0), quindi non può portare, ad esempio, `00:1F:20` (Logitech). Con un solo chip, al massimo una identità può avere l'OUI di un marchio.
3. L'abbinamento (bonding) sugli host è per indirizzo: ogni identità va abbinata e ricordata separatamente.

## Raccomandazione
- **Restare sul dispositivo composito** già implementato (un'identità, funzioni mouse + tastiera): funziona sul chip attuale ed è verificato.
- Per identità davvero separate e simultanee: un **ESP32-S3 o C3** (BLE 5, più istanze di advertising con indirizzi propri) oppure **più schede**. Non l'ho provato: non ho l'hardware.
- Prima di comprare hardware, ripetere il test della connessione doppia con un secondo computer o telefono come centrale, per escludere il limite del Mac.
