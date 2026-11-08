# AM32 - RC Car Firmware Modifikationen

Diese Firmware-Erweiterung optimiert AM32 speziell für **(Micro) RC-Cars, Basher und Racer**. Sie ersetzt das standardmäßige Multicopter-Verhalten durch eine vollwertige RC-Car-Regler-Logik mit Getriebeschutz, Freilauf und feinfühlig einstellbaren Parametern über den AM32-Web-Configurator.

---

## 🚀 Übersicht der Features

### 1. Freilauf bei Gasrücknahme (Kein Damped Light beim Fahren)
* **Problem bisher:** Multicopter nutzen komplementäre PWM (Damped Light). Bei Gasrücknahme (z. B. von 80 % auf 30 %) schalteten die Low-Side-FETs ein und bremsten den Motor aktiv regenerativ gegen $\rightarrow$ ruckartiges Fahrverhalten, ungewolltes Einnicken.
* **Neu:** Während der Fahrt (sowohl vorwärts als auch rückwärts) bleibt die Low-Side-PWM aus (`temp_comp_pwm = 0`). 
* **Effekt:** Das Auto rollt beim Gaswegnehmen weich und frei auf die neue Geschwindigkeit bzw. zum Stillstand aus (Dioden-Freilauf).

### 2. Intelligente Verzögerungsregelung (`Brake strength`) mit dynamischem Re-Grip ABS (Release 2.22_8)
* **Steuerung:** Über den Schieberegler **`Brake strength`** (im Code `drag_brake_strength`, Werte 1 bis 10) im Configurator.
* **Grip-Level / Maximalverzögerung (in RPM/ms):**
  * Der Schieberegler fungiert nun als Kalibrierung der **maximal physikalisch fahrbaren Verzögerungsrate** ($\text{Stufe} \times 3\,\text{RPM/ms}$):
  * **Stufe 1:** $-3\,\text{RPM/ms}$ (sanftes, langes Ausrollen für Glatteis / lose Erde)
  * **Stufe 5:** $-15\,\text{RPM/ms}$ (ausgewogene Straßenverzögerung)
  * **Stufe 10:** $-30\,\text{RPM/ms}$ (maximaler Bremsbiss für High-Grip)
* **Geschlossener 1-kHz-Verzögerungsregelkreis (Closed-Loop Deceleration Tracking):**
  * **Linearer Drehzahlabfall:** Die Drehzahl fällt bei konstant gehaltenem Hebelweg exakt linear bis zum Stillstand ab.
  * **Schrittgeschwindigkeit / Stillstand:** Bei sinkender Drehzahl (wegbrechender Gegen-EMK) zieht der Regler die FETs automatisch stufenlos bis auf 100 % Phasenkurzschluss zu, sodass selbst ein sanfter Hebelbefehl das Auto zuverlässig auf den Punkt zum Stehen bringt.
* **Dynamisches Re-Grip ABS (Keine starren Timer, echtes Fahrbahn-Tracking):**
  * **Sofortige Notentlastung:** Bricht die Raddrehzahl schneller ein als die Soll-Rampe (Blockadegefahr), schaltet der Regler die Bremse **augenblicklich zu 100 % auf Freilauf (`allOff()`)**.
  * **Glasklare BEMF:** Im Freilauf sind alle FETs aus $\rightarrow$ AM32 sieht das absolut störungsfreie BEMF-Signal und beobachtet, wie der rollende Asphalt das Rad antreibt.
  * **Beschleunigungs-Wächter:** Solange die Raddrehzahl steigt ($d\omega/dt > 0$), bleibt die Bremse voll geöffnet.
  * **Neuer Bremszyklus bei Plateau:** Sobald das Rad nicht weiter beschleunigt (Rollgeschwindigkeit über Grund erreicht), startet der Regler sofort den nächsten Bremsversuch ab dieser erholten Geschwindigkeit (Sägezahn-Profil).
* **Brutale 100 % Rückwärts-Bremse (Knackige Wende Rückwärts $\rightarrow$ Vorwärts):**
  * Beim Bremsen aus Rückwärtsfahrt (Antippen von Gas nach vorne) greift **sofort und kompromisslos 100 % Vollbremsung (`fullBrake()`)**.
  * Der Motor stoppt die Rückwärtsdrehung augenblicklich und schießt ohne Verzögerung vorwärts los.

### 3. Einstellbare Rückfahrgeschwindigkeit (`Running brake level`)
* **Steuerung:** Über den Schieberegler **`Running brake level`** (im Code `driving_brake_strength`, Werte 1 bis 10) im Configurator.
* **Skalierung:**
  * **Stufe 10:** 100 % Rückwärts-Leistung (volle Power)
  * **Stufe 5:** 50 % Rückwärts-Leistung
  * **Stufe 3:** 30 % Rückwärts-Leistung (optimal für enges Rangieren ohne Durchdrehen)
* **Volle Auflösung:** Der Hebelweg des Senders bleibt auch bei reduzierter Rückwärts-Power zu 100 % erhalten – du hast die volle Präzision und Feinfühligkeit.

### 4. Symmetrischer Getriebeschutz & Sofort-Wende (Rückwärts $\rightarrow$ Vorwärts)
* Fährt das Auto rückwärts und du gibst Vorwärtsgas:
  * Das Vorwärtsgas greift sofort mit **100 % Vollbremsung (`fullBrake()`)**, um die Rückwärtsdrehung in wenigen Millisekunden zu stoppen.
  * Sobald die sensorlose Kommutierung Stillstand erkennt (`running == 0`), schaltet der Regler **ohne Verzögerung direkt in Vorwärtsfahrt** um und beschleunigt los.
  * Der Hebel muss **nicht** erst in Neutral losgelassen werden!

### 5. Double-Tap & Stillstandsschutz (Vorwärts $\rightarrow$ Rückwärts)
* Fährst du vorwärts und drückst nach hinten:
  * Wirkt als proportionale Verzögerungsregelung bis zum Stillstand.
  * Erst wenn der Gashebel **Neutral durchläuft UND der Motor tatsächlich steht**, wird der Rückwärtsgang freigegeben.
  * Erst der nächste Druck nach hinten fährt rückwärts an.
  * Rollt das Auto noch schnell vorwärts und man tippt am Hebel, bleibt der Rückwärtsgang gesperrt und bremst weiterhin sicher ab (Getriebeschutz).

### 6. Freilauf in Neutral (Keine automatische Handbremse)
* Obwohl in der GUI `Brake on stop` aktiv gesetzt wird (um den Schieber freizuschalten), greift in Neutralstellung **keine automatische Blockierbremse**.
* Bei Hebel in Neutral schaltet der Regler immer auf Freilauf (`allOff()`), sodass das Auto natürlich ausrollt.

### 7. Globaler Bugfix der AM32 `map()`-Funktion (`Src/functions.c`)
* **Problem im AM32-Original:** Die `map()`-Funktion prüfte Grenzüberschreitungen vor der Umkehrprüfung (`if (in_min > in_max)`). Bei fallenden Wertebereichen (z. B. Bremsweg von 950 nach 0) gab `map()` für alle Werte $\ge 0$ sofort `out_max` (100 %) zurück $\rightarrow$ digitaler Vollanker ohne Dosierbarkeit.
* **Lösung:** `map()` prüft umgekehrte Wertebereiche nun an erster Stelle, und die Bremsmodulation rechnet direkt per Dreisatz.

---

## 🎛️ Mapping im AM32-Web-Configurator

| GUI-Element | EEPROM-Feld | Funktion im Car-Modus |
| :--- | :--- | :--- |
| **`Brake strength`** (1–10) | `drag_brake_strength` | **Maximalverzögerung / Grip-Level** (3 bis 30 RPM/ms, ABS-Grenze) |
| **`Running brake level`** (1–10) | `driving_brake_strength` | **Maximale Rückfahrgeschwindigkeit** (10 % bis 100 %) |
| **`Car type reverse braking`** | `rc_car_reverse` | Fest aktiviert (`1`) |
| **`3D mode`** | `bi_direction` | Fest aktiviert (`1`) |
| **`Brake on stop`** | `brake_on_stop` | GUI-Freischalter (`1`), im Fahrbetrieb Freilauf in Neutral |

---

## 📜 Release-Historie

* **2.22_1:** Initialer Car-Modus (Freilauf bei Gasrücknahme, symmetrischer Getriebeschutz, Schieberegler-Freischaltung).
* **2.22_4:** Entkopplung des internen Rückwärtsgang-Status (`car_reverse_active`) von `dir_reversed`.
* **2.22_5:** Behebung des AM32 `map()`-Bugs bei umgekehrten Zahlenbereichen; direkte Hebel-Dreisatzberechnung.
* **2.22_6:** Vollständige Entfernung aller künstlichen Brems-Zeitverzögerungen/Rampen; 100 % Sofort-Biss beim Wenden aus Rückwärtsfahrt.
* **2.22_7:** Einführung des **geschlossenen 1-kHz-Verzögerungsregelkreises mit Anti-Lock (ABS)** und Kalibrierung über `Brake strength` (3 bis 30 RPM/ms).
* **2.22_8:** Implementierung der dynamischen Re-Grip ABS State-Machine mit Freilauf-Plateau-Erkennung.
* **2.22_9:** **Low-Grip & Stall-Erkennung Bugfix:**
  * **Echtzeit-Stall-Erkennung:** Erkennung überfälliger Kommutierungen via `INTERVAL_TIMER_COUNT` (1–2 ms Reaktionszeit statt 22,5 ms Blindheit).
  * **Behebung des Anti-Windup-Clamps:** Das Deckeln von `brake_target_rpm` nach unten wurde entfernt, sodass echter Radschlupf ($v_{\text{Fahrzeug}} - v_{\text{Rad}}$) auf rutschigem Untergrund zuverlässig ABS auslöst.
  * **BEMF-Interrupt-Wächter im Freilauf:** Verhinderung des Einschlafens von AM32 (`running = 0`, `maskPhaseInterrupts`) bei Bremsungen aus Fahrt, sodass die BEMF des wieder anrollenden Rades auch auf glattestem Untergrund sofort erfasst wird.

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
