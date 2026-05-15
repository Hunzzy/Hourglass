# Digitale Sanduhr (ESP32-C3 CodeCell)

Dieses Projekt realisiert eine digitale Sanduhr mit realistischer Physik-Simulation und Gravitations-Erkennung. Mithilfe der **CodeCell (ESP32-C3 Mini)** und dem integrierten **BNO085 Bewegungssensor** erkennt die Sanduhr ihre Ausrichtung und lässt den "Sand" (LED-Punkte) immer nach unten fallen.

## Features
- **Realistische Sand-Physik:** Die Sandkörner bewegen sich basierend auf der Neigung.
- **Duale Matrix-Anzeige:** Zwei kaskadierte (daisy-chained) 8x8 LED-Matrizen.
- **Präziser Timer:** Anzeige der verbleibenden Zeit auf einem TM1637 7-Segment Display.
- **Einstellbare Zeit:** Zeitwahl (1–99 Min) via Potentiometer.
- **Interaktive Steuerung:** Start/Reset über einen Taster und akustisches Feedback via Buzzer.

## Hardware-Komponenten
- **Controller:** CodeCell ESP32-C3 Mini (mit integriertem BNO085 Sensor)
- **Anzeige 1:** 2x 8x8 LED-Matrix mit MAX7219 Treibern (Daisy-Chain)
- **Anzeige 2:** TM1637 4-Digit 7-Segment Display
- **Input:** 10k Ohm Potentiometer & Momentary Push-Button
- **Output:** Aktives Buzzer-Modul (3-Pin)

## Verkabelung (Pin-Belegung)

### LED-Matrizen (MAX7219)
| Matrix Pin | ESP32-C3 Pin | Funktion |
|------------|--------------|----------|
| VCC        | 4.4V (USB)   | Stromversorgung |
| GND        | GND          | Masse |
| DIN        | G7           | Data In (nur 1. Display) |
| CLK        | G5           | Clock |
| CS         | G6           | Chip Select |

### 7-Segment Display (TM1637)
| TM1637 Pin | ESP32-C3 Pin |
|------------|--------------|
| CLK        | G8           |
| DIO        | G9           |
| VCC        | 3.3V         |
| GND        | GND          |

### Sensoren & Kontrollen
- **Potentiometer (Mitte):** G1 (ADC)
- **Taster:** G2 (Interner Pull-up genutzt)
- **Buzzer (I/O):** G3

---

## Software-Installation

### 1. Arduino IDE Vorbereitung
Stelle sicher, dass du die aktuelle [Arduino IDE](https://www.arduino.cc/en/software) installiert hast und das ESP32 Board-Paket hinzugefügt wurde:
- Gehe zu `Datei` -> `Voreinstellungen`.
- Füge unter "Zusätzliche Boardverwalter-URLs" hinzu: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Wähle unter `Werkzeuge` -> `Board` -> `ESP32` das **ESP32C3 Dev Module**.

### 2. Benötigte Bibliotheken
Installiere die folgenden Bibliotheken direkt über den **Library Manager** (`Werkzeuge` -> `Bibliotheken verwalten`):

1.  **LedControl** (von Eberhard Fahle) – Steuerung der LED-Matrizen.
2.  **TM1637** (von Avishay Orpaz) – Steuerung der 7-Segment Anzeige.
3.  **CodeCell** (von CodeCell) – Zugriff auf den internen BNO085 Sensor.

### 3. Wichtige Hinweise zum Kompilieren
- **Pfad-Fehler vermeiden:** Achte darauf, dass dein Projektordner **keine Umlaute** (ä, ö, ü) im Pfad enthält (z. B. nicht unter `C:\Benutzer\Schüler\`). Verschiebe den Ordner stattdessen nach `C:\Projekte\Sanduhr`.
- **Stromversorgung:** Da die LED-Matrizen bei voller Helligkeit viel Strom ziehen, sollte die Helligkeit im Code (`setIntensity`) niedrig eingestellt bleiben (z. B. Stufe 2), wenn die Versorgung rein über USB erfolgt.

## Bedienung
1. **Einstellen:** Drehe am Potentiometer, um die gewünschte Zeit auf dem 7-Segment Display zu sehen.
2. **Starten:** Drücke den Taster kurz. Der Sand beginnt in der oberen Matrix zu fallen.
3. **Umdrehen:** Drehe das Gerät physikalisch um. Der Sand reagiert sofort auf die neue Gravitationsrichtung.
4. **Abbrechen:** Halte den Taster für 2 Sekunden gedrückt, um zum Einstellmodus zurückzukehren.
