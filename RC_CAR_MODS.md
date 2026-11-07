# AM32 - RC Car Firmware Modifikationen

Diese Firmware-Erweiterung optimiert AM32 speziell für **(Micro) RC-Cars, Basher und Racer**. Sie ersetzt das standardmäßige Multicopter-Verhalten durch eine vollwertige RC-Car-Regler-Logik mit Getriebeschutz, Freilauf und feinfühlig einstellbaren Parametern über den AM32-Web-Configurator.

---

## 🚀 Übersicht der Features

### 1. Freilauf bei Gasrücknahme (Kein Damped Light beim Fahren)
* **Problem bisher:** Multicopter nutzen komplementäre PWM (Damped Light). Bei Gasrücknahme (z. B. von 80 % auf 30 %) schalteten die Low-Side-FETs ein und bremsten den Motor aktiv regenerativ gegen $\rightarrow$ ruckartiges Fahrverhalten, ungewolltes Einnicken.
* **Neu:** Während der Fahrt (sowohl vorwärts als auch rückwärts) bleibt die Low-Side-PWM aus (`temp_comp_pwm = 0`). 
* **Effekt:** Das Auto rollt beim Gaswegnehmen weich und frei auf die neue Geschwindigkeit bzw. zum Stillstand aus (Dioden-Freilauf).

### 2. Einstellbare Betriebsbremskraft (`Brake strength`) mit Highspeed-Snapshot & Expo
* **Steuerung:** Über den Schieberegler **`Brake strength`** (im Code `drag_brake_strength`, Werte 1 bis 10) im Configurator.
* **Skalierung:**
  * **Stufe 1:** 10 % maximale Bremskraft (sehr sanftes Abbremsen, schont Micro-Getriebe)
  * **Stufe 5:** 50 %
  * **Stufe 8:** 80 %
  * **Stufe 10:** 100 % (volle Phasenkurzschluss-Bremse `fullBrake()`, absolut identisch zur maximalen Hardware-Bremsleistung des Original-AM32)
* **Intelligente Highspeed-Snapshot-Dämpfung mit automatischem 400-ms-Blend-In:**
  * **Einsatz:** Im Moment des Tretens auf die Bremse nimmt die Firmware einen Schnappschuss der aktuellen Drehzahl (`average_interval`).
  * **Highspeed (z. B. 50 km/h, < 150 µs):** Dämpft die Bremskraft initial auf 35 %, um die extreme Back-EMF-Spitze und blockierende Hinterräder bei entlasteter Hinterachse komplett zu eliminieren.
  * **Schneckentempo (> 1000 µs):** Volle 100 % Bremskraft ab der ersten Millisekunde.
  * **1-kHz-Blend-In:** Über ca. 400 ms blendet die Firmware die Dämpfung im 1-kHz-Loop stufenlos auf 100 % hoch. So fängt der Regler den Highspeed-Schlag ab, verzögert dann kontinuierlich und bringt das Auto sicher bei 0 RPM zum Stillstand.
* **30 % Progressive Expo-Kennlinie (RC-Sender-Standard für Vorwärts-Bremse):**
  * Verwendet eine kubische Mischung (70 % linear + 30 % kubisch) auf dem Hebelweg nach hinten.
  * **0 bis 40 % Hebelweg:** Extrem feinfühlige Bremswirkung (nur ~14–30 % Kraft). Erlaubt feinstes Anbremsen ohne Heckausbruch auf 2WD-Chassis.
  * **Ab 70 % Hebelweg:** Zügiger Anstieg auf das über den Slider gewählte Maximum für Notbremsungen.
* **Knackige Wende bei Rückwärtsfahrt (Rückwärts $\rightarrow$ Vorwärts):**
  * Beim Bremsen aus Rückwärtsfahrt (Hebel nach vorne) greift keine Expo-Dämpfung: Der Motor stoppt sofort und schießt ohne Verzögerung vorwärts los.

### 3. Einstellbare Rückfahrgeschwindigkeit (`Running brake level`)
* **Steuerung:** Über den Schieberegler **`Running brake level`** (im Code `driving_brake_strength`, Werte 1 bis 10) im Configurator.
* **Skalierung:**
  * **Stufe 10:** 100 % Rückwärts-Leistung (volle Power)
  * **Stufe 5:** 50 % Rückwärts-Leistung
  * **Stufe 3:** 30 % Rückwärts-Leistung (optimal für enges Rangieren ohne Durchdrehen)
* **Volle Auflösung:** Der Hebelweg des Senders bleibt auch bei reduzierter Rückwärts-Power zu 100 % erhalten – du hast die volle Präzision und Feinfühligkeit.

### 4. Symmetrischer Getriebeschutz (Rückwärts $\rightarrow$ Vorwärts)
* Fährt das Auto rückwärts und du gibst Vorwärtsgas:
  * Das Vorwärtsgas wirkt als **dosierte Bremse bis zum Stillstand (0 RPM)**.
  * Sobald die sensorlose Kommutierung Stillstand erkennt (`running == 0`), schaltet der Regler **ohne Verzögerung direkt in Vorwärtsfahrt** um und beschleunigt los.
  * Der Hebel muss **nicht** erst in Neutral losgelassen werden!

### 5. Double-Tap & Stillstandsschutz (Vorwärts $\rightarrow$ Rückwärts)
* Fährst du vorwärts und drückst nach hinten:
  * Wirkt proportional als Bremse bis zum Stillstand.
  * Erst wenn der Gashebel **Neutral durchläuft UND der Motor tatsächlich steht**, wird der Rückwärtsgang freigegeben.
  * Erst der nächste Druck nach hinten fährt rückwärts an.
  * Rollt das Auto noch schnell vorwärts und man tippt am Hebel, bleibt der Rückwärtsgang gesperrt und bremst weiterhin sicher ab (Getriebeschutz).

### 6. Freilauf in Neutral (Keine automatische Handbremse)
* Obwohl in der GUI `Brake on stop` aktiv gesetzt wird (um den Schieber freizuschalten), greift in Neutralstellung **keine automatische Blockierbremse**.
* Bei Hebel in Neutral schaltet der Regler immer auf Freilauf (`allOff()`), sodass das Auto natürlich ausrollt.

---

## 🎛️ Mapping im AM32-Web-Configurator

| GUI-Element | EEPROM-Feld | Funktion im Car-Modus |
| :--- | :--- | :--- |
| **`Brake strength`** (1–10) | `drag_brake_strength` | **Maximale Bremskraft** (10 % bis 100 %) |
| **`Running brake level`** (1–10) | `driving_brake_strength` | **Maximale Rückfahrgeschwindigkeit** (10 % bis 100 %) |
| **`Car type reverse braking`** | `rc_car_reverse` | Fest aktiviert (`1`) |
| **`3D mode`** | `bi_direction` | Fest aktiviert (`1`) |
| **`Brake on stop`** | `brake_on_stop` | GUI-Freischalter (`1`), im Fahrbetrieb Freilauf in Neutral |

---

## 🛠️ Build-Anleitung

Die Build-Tools befinden sich in `tools/windows/`. Gebaut wird per GNU Make:

```powershell
# Für das KingModel F421 Target:
.\tools\windows\make\bin\make.exe KINGMODEL_CAR_F421

# Für ein beliebiges anderes AM32-Target (z. B. G071, F051 etc.):
.\tools\windows\make\bin\make.exe <TARGET_NAME>

# Clean Build:
.\tools\windows\make\bin\make.exe clean
```

Die fertigen Binaries (`.hex` und `.bin`) liegen anschließend im Verzeichnis `obj/` und können über den AM32-Configurator geflasht werden.
