# AM32 - RC Car Firmware Modifikationen

Diese Firmware-Erweiterung optimiert AM32 speziell für **(Micro) RC-Cars, Basher und Racer**. Sie ersetzt das standardmäßige Multicopter-Verhalten durch eine vollwertige RC-Car-Regler-Logik mit Getriebeschutz, Freilauf und feinfühlig einstellbaren Parametern über den AM32-Web-Configurator.

---

## 🚀 Übersicht der Features

### 1. Freilauf bei Gasrücknahme (Kein Damped Light beim Fahren)
* **Problem bisher:** Multicopter nutzen komplementäre PWM (Damped Light). Bei Gasrücknahme (z. B. von 80 % auf 30 %) schalteten die Low-Side-FETs ein und bremsten den Motor aktiv regenerativ gegen $\rightarrow$ ruckartiges Fahrverhalten, ungewolltes Einnicken.
* **Neu:** Während der Fahrt (sowohl vorwärts als auch rückwärts) bleibt die Low-Side-PWM aus (`temp_comp_pwm = 0`). 
* **Effekt:** Das Auto rollt beim Gaswegnehmen weich und frei auf die neue Geschwindigkeit bzw. zum Stillstand aus (Dioden-Freilauf).

### 2. Einstellbare Betriebsbremskraft (`Brake strength`) - Linear & Rampenfrei (Release 2.22_6)
* **Steuerung:** Über den Schieberegler **`Brake strength`** (im Code `drag_brake_strength`, Werte 1 bis 10) im Configurator.
* **Skalierung:**
  * **Stufe 1:** 10 % maximale Bremskraft (sehr sanftes Abbremsen, schont Micro-Getriebe)
  * **Stufe 5:** 50 %
  * **Stufe 8:** 80 %
  * **Stufe 10:** 100 % (volle Phasenkurzschluss-Bremse `fullBrake()`, absolut identisch zur maximalen Hardware-Bremsleistung des Original-AM32)
* **Direktes, lineares Ansprechverhalten ohne Rampen (Instant Response):**
  * **100 % rampenfrei:** Keine künstlichen Zeitfilter, kein verzögerndes Blend-In – der Bremsbefehl liegt exakt und ohne Millisekunden-Verlust an den FETs an.
  * **Direkt & linear:** Kein flaches Expo mehr, das die Bremse bei Langsamfahrt wirkungslos macht. Bereits ab den ersten Prozenten Hebelweg baut die Bremse verlässlich spürbare Verzögerung auf.
* **Brutale 100 % Rückwärts-Bremse (Knackige Wende Rückwärts $\rightarrow$ Vorwärts):**
  * Beim Bremsen aus Rückwärtsfahrt (Antippen von Gas nach vorne) greift **sofort und kompromisslos 100 % Vollbremsung (`fullBrake()`)**.
  * Da man aus Rückwärtsfahrt ohnehin nur leicht Gas antippt, stoppt der Motor die Rückwärtsdrehung augenblicklich und schießt ohne Verzögerung vorwärts los.

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

### 7. Selbstlernendes Sensorless BLDC-ABS (Anti-Blockier-System)
* **Physikalischer Massensprung-Erkenner (360°-Fenster):**
  * Bei einem 2-Pol-Motor entspricht eine elektrische Umdrehung (6 Kommutierungsschritte = 360°) exakt einer mechanischen Rotorumdrehung.
  * Durch die rollierende Auswertung des 360°-Fensters werden Pol-Asymmetrien und Komparator-Offsets ohne Phasenverzögerung (Group Delay) eliminiert.
  * Solange die Reifen haften, verzögert der Motor die gesamte Fahrzeugmasse ($J_{\text{Auto}}$). Sobald das Rad abreißt/blockiert, koppelt sich die Fahrzeugmasse ab und nur noch der leichte Antriebsstrang ($J_{\text{Antrieb}}$) wird gebremst $\rightarrow$ die Periodenverlängerung $\Delta T$ springt um Faktor 5 bis 20!
* **Selbstlernende Fahrzeug-Baseline im RAM:**
  * Ein IIR-Tiefpassfilter lernt im Fahrbetrieb den normalen Verzögerungs-Quotienten $Q = \Delta T / \text{Duty}$ des Fahrzeugs.
  * Die ABS-Schlupfschwelle adaptiert sich dynamisch: $\text{lock\_threshold} \approx 2{,}25 \times Q \times \text{Duty}$.
* **4-Phasen-Regelkreis & Pfützen-feste 2-Stufen-Rampe:**
  1. **`ABS_BUILD`:** Druckaufbau Richtung Fahrer-Sollwert. Bei Abriss ($\text{confidence} \ge 2$) $\rightarrow$ Wechsel zu `ABS_DUMP`.
  2. **`ABS_DUMP`:** Sofortiger Freilauf ($0\,\%$ Bremsmoment). Das Schleppmoment hält den Antriebsstrang auf der Bremsflanke. Komparator überwacht das Wiederanlaufen ($\Delta T \le 0$).
  3. **`ABS_HOLD` (Stufe A Fast-Jump):** Springt sofort auf $85\,\%$ von `last_lock_duty`. Bei extrem steilem Wiederanlauf (Pfützen-Erkennung: Low-to-High-$\mu$) springt das ABS stattdessen Richtung $60\,\%$ von `peak_lock_duty` (Asphalt-Niveau).
  4. **`ABS_REAPPLY` (Stufe B Progressive Such-Rampe):** Steigt mit schrittweise verdoppelter Schrittweite zügig wieder Richtung Zielbremswert.
* **Fallbacks & Notbremse:**
  * **In-Air / Panic Override:** Bei $\ge 95\,\%$ Bremshebelweg wird das ABS komplett überbrückt (100 % Notbremse & Fluglagenkorrektur bei Sprüngen).
  * **Standstill-Fallback:** Bei natürlichem BEMF-Abriss kurz vor dem Stillstand (`INTERVAL_TIMER_COUNT > 45000`) schaltet das System nahtlos auf die statische Haltebremse um.

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
