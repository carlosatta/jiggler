# Jiggler: più dispositivi, profili, script, modalità AP — design

Estende [funzionalita.md](../../../funzionalita.md). Richiesta dell'utente (2026-10-23): captive portal, più dispositivi contemporanei (mouse, tastiera…), tipo/modello da lista (Logitech, Razer…) o custom con nome/MAC/ecc. e valori di default, tastiera con comportamento scriptabile, modalità senza WiFi di casa. L'utente ha rinunciato alle domande ("fai quello che vuoi, voglio risultati"): **le scelte sotto sono mie**, non concordate.

## Obiettivo
Il dispositivo espone mouse e tastiera BLE, ciascuno scelto da un catalogo di profili o personalizzato; il comportamento si programma con script; funziona anche senza WiFi di casa (rete propria con captive portal e dashboard).

## Non-obiettivi
Gamepad, telecomando multimediale, layout tastiera non US, protocollo proprietario (HID++), OTA, più schede.

## Anti-obiettivi
Rompere il mouse esistente, l'API esistente o i test esistenti; richiedere WiFi per far funzionare il Bluetooth; lasciare la dashboard/API raggiungibili senza token.

## Scelte mie
1. **Una sola identità Bluetooth.** L'ESP32 classico ha un solo indirizzo BLE: mouse e tastiera sono funzioni (collezioni HID) di un unico dispositivo composito. Identità (nome, produttore, VID/PID, MAC) = quella del *primo dispositivo abilitato*; le altre contribuiscono solo il tipo. "Identità separate simultanee" resta un'ipotesi verificata da una prova usa-e-getta (ultimo task); si implementa solo se la prova riesce.
2. **Modello dati:** lista `devices` (`id`, `kind` mouse|keyboard, `enabled`, `profileId`, override opzionali `name`, `manufacturer`, `vid`, `pid`, `mac`). Profilo `custom` = valori generici. Default: mouse abilitato "Logitech MX Master 3S" (come oggi), tastiera disabilitata. Cambiare identità o abilitare la tastiera richiede riavvio e nuovo abbinamento sul computer.
3. **Catalogo:** solo profili con VID/PID/OUI verificati su fonti pubbliche; quelli non verificabili non entrano.
4. **Script:** JSON (`move`, `click`, `scroll`, `key`, `type`, `wait`, `repeat`), compilato in un `Plan` eseguito dallo stesso `runPlan`. Libreria in NVS (max 8 script), esecuzione da dashboard/API/MCP e pianificazione con intervalli casuali ("automazioni"). Layout tastiera US.
5. **Modalità rete:** `auto` (come oggi: WiFi di casa, portale di setup se manca) oppure `ap` (rete propria con password, captive portal DNS che porta alla dashboard). Cambio da dashboard/API o tenendo premuto BOOT 3 s; sempre protetto da token.
6. **Compatibilità:** `Controller`, `/api/status|config|start|…` e i tool MCP esistenti restano invariati; le novità sono nuove rotte/tool.
