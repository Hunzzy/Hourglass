/*
 * ============================================================
 *  Digitale Sanduhr — ESP32-C3 Mini (CodeCell)
 *  Bibliotheken NUR aus dem Arduino Library Manager
 * ============================================================
 *
 *  BENÖTIGTE BIBLIOTHEKEN (Sketch → Bibliotheken → Verwalten):
 *  ─────────────────────────────────────────────────────────
 *  • "MD_MAX72XX"     von MajicDesigns   → MAX7219 LED-Matrizen
 *  • "TM1637Display"  von Avishay Orpaz  → 4-Digit 7-Segment
 *  • "CodeCell"       von Microbots      → ESP32-C3 + BNO085
 *
 *  VERDRAHTUNG
 *  ─────────────────────────────────────────────────────────
 *  LED-Matrizen (daisy-chained, 2× MAX7219):
 *    G7        → DIN   (1. Display)
 *    G5        → CLK
 *    G6        → CS
 *    4.4V USB  → VCC
 *    GND       → GND
 *    DOUT 1. Display → DIN 2. Display
 *
 *  TM1637 (4-Digit Display):
 *    G8   → CLK
 *    G9   → DIO
 *    3.3V → VCC
 *    GND  → GND
 *
 *  Potentiometer (3-Pin):
 *    Pin 1 (links)  → 3.3V
 *    Pin 2 (Mitte)  → G1   (ADC)
 *    Pin 3 (rechts) → GND
 *
 *  Taster (2-Pin):
 *    Pin A → G2
 *    Pin B → GND   (interner Pull-up, kein Widerstand nötig)
 *
 *  Buzzer-Modul (aktiv, 3-Pin):
 *    VCC → 3.3V
 *    GND → GND
 *    I/O → G3      (HIGH = Ton AN)
 *
 *  HINWEIS ZUM HARDWARE-TYPE:
 *  ─────────────────────────────────────────────────────────
 *  MD_MAX72XX braucht den richtigen Hardware-Typ deines Moduls.
 *  Die meisten günstigen 8×8 Module aus dem Internet sind FC16_HW.
 *  Falls die Anzeige gespiegelt/gedreht ist → GENERIC_HW probieren.
 *
 *  FUNKTIONSWEISE
 *  ─────────────────────────────────────────────────────────
 *  • Potentiometer stellt Minuten ein (1–99)
 *  • 4-Digit Display zeigt MM:SS
 *  • Taster kurz drücken  → Countdown starten
 *  • Taster ≥2s halten    → Countdown abbrechen
 *  • Sand-Animation auf den LED-Matrizen
 *  • BNO085 erkennt Umdrehung → Sanduhr dreht sich um
 *  • Nach Ablauf: Buzzer-Signal, Taster drücken → Reset
 * ============================================================
 */

#include <MD_MAX72xx.h>
#include <TM1637Display.h>
#include <CodeCell.h>

// ── Hardware-Typ ──────────────────────────────────────────────────────────────
// FC16_HW   → die meisten günstigen Module (Standard)
// GENERIC_HW → alternative falls gespiegelt/gedreht
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

// ── Pin-Definitionen ──────────────────────────────────────────────────────────
#define PIN_DIN      7
#define PIN_CLK      5
#define PIN_CS       6

#define PIN_POT      1    // Potentiometer (ADC)
#define PIN_BUTTON   2    // Taster (aktiv LOW)
#define PIN_BUZZER   3    // Aktives Buzzer-Modul (HIGH = AN)

#define PIN_TM_CLK   8
#define PIN_TM_DIO   9

// ── Konstanten ────────────────────────────────────────────────────────────────
#define NUM_DEVICES       2      // Anzahl MAX7219 Module
#define MATRIX_TOP        0      // Adresse obere Matrix (MD_MAX72XX zählt von rechts)
#define MATRIX_BOTTOM     1      // Adresse untere Matrix
#define MAX_GRAINS        60     // Sandkörner gesamt
#define GRAVITY_THR       4.5f   // m/s² Schwellenwert Umdrehung
#define LONG_PRESS_MS     2000   // ms für langen Tasterdruck (Abbruch)
#define ANIM_DELAY_MS     30     // ms pro Animations-Frame
#define BEEP_ON_MS        150    // ms Buzzer AN
#define BEEP_OFF_MS       150    // ms Pause zwischen Pieptönen
#define REPEAT_BEEP_MS    5000   // ms zwischen Wiederholungs-Beeps (FINISHED)

// ── Objekte ───────────────────────────────────────────────────────────────────
MD_MAX72XX mx(HARDWARE_TYPE, PIN_DIN, PIN_CLK, PIN_CS, NUM_DEVICES);
TM1637Display tm(PIN_TM_CLK, PIN_TM_DIO);
CodeCell myCodeCell;

// ── Pixel-Zustandsspeicher (8×8 pro Matrix, 2 Matrizen) ─────────────────────
// mx.getPoint() ist langsam → eigene bool-Arrays als Cache
bool grid[NUM_DEVICES][8][8];  // grid[device][col][row]  col/row = 0..7

// ── Zustands-Maschine ─────────────────────────────────────────────────────────
enum State { SETTING, RUNNING, FINISHED };
State state = SETTING;

// ── Variablen ─────────────────────────────────────────────────────────────────
int  topDev        = MATRIX_TOP;
int  botDev        = MATRIX_BOTTOM;
int  setMinutes    = 5;
long totalSec      = 0;
long remainSec     = 0;
int  grainsMoved   = 0;
long grainInterval = 1000;

// Taster
bool          btnWasDown    = false;
unsigned long btnPressStart = 0;

// Buzzer (non-blocking)
int           beepTotal     = 0;
int           beepDone      = 0;
bool          buzzerIsOn    = false;
unsigned long buzzerUntil   = 0;
unsigned long lastRepeatBeep= 0;

// Timing (ersetzt Delay.h komplett mit millis())
unsigned long grainNextMs   = 0;
unsigned long displayNextMs = 0;


// ═════════════════════════════════════════════════════════════════════════════
//  PIXEL-CACHE HILFSFUNKTIONEN
//  MD_MAX72XX verwendet col 0..7, row 0..7
//  col = x (links→rechts), row = y (oben→unten)
// ═════════════════════════════════════════════════════════════════════════════

bool getPixel(int dev, int col, int row) {
  return grid[dev][col][row];
}

void setPixel(int dev, int col, int row, bool on) {
  grid[dev][col][row] = on;
  mx.setPoint(row, dev * 8 + col, on);  // MD_MAX72XX: globale Spalte
}

void clearGrid(int dev) {
  for (int c = 0; c < 8; c++)
    for (int r = 0; r < 8; r++)
      setPixel(dev, c, r, false);
}

void clearAllGrids() {
  mx.clear();
  memset(grid, 0, sizeof(grid));
}

// Füllt eine Matrix mit n Körnern (zeilenweise von oben links → unten rechts)
void fillGrid(int dev, int grains) {
  clearGrid(dev);
  int count = 0;
  for (int r = 0; r < 8 && count < grains; r++)
    for (int c = 0; c < 8 && count < grains; c++) {
      setPixel(dev, c, r, true);
      count++;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
//  SAND-PHYSIK
//  Körner fallen nach unten (row++ = nach unten)
//  Bei Blockierung: zufällig nach links oder rechts ausweichen
// ═════════════════════════════════════════════════════════════════════════════

void updateParticles(int dev) {
  // Von unten nach oben scannen damit Körner korrekt kaskadieren
  for (int r = 6; r >= 0; r--) {
    for (int c = 0; c < 8; c++) {
      if (!getPixel(dev, c, r)) continue;

      bool canDown  = !getPixel(dev, c,     r + 1);
      bool canLeft  = (c > 0) && !getPixel(dev, c - 1, r + 1);
      bool canRight = (c < 7) && !getPixel(dev, c + 1, r + 1);

      if (canDown) {
        setPixel(dev, c, r,     false);
        setPixel(dev, c, r + 1, true);
      } else if (canLeft && !canRight) {
        setPixel(dev, c,     r, false);
        setPixel(dev, c - 1, r + 1, true);
      } else if (canRight && !canLeft) {
        setPixel(dev, c,     r, false);
        setPixel(dev, c + 1, r + 1, true);
      } else if (canLeft && canRight) {
        bool goRight = ((millis() / 10 + c + r) % 2 == 0);
        setPixel(dev, c,              r, false);
        setPixel(dev, goRight ? c+1 : c-1, r + 1, true);
      }
    }
  }
}

// Überträgt ein Korn von topDev (unterste Zeile) nach botDev (oberste Zeile)
bool transferGrain() {
  // Suche das unterste-rechteste Pixel in der oberen Matrix
  for (int r = 7; r >= 0; r--) {
    for (int c = 7; c >= 0; c--) {
      if (!getPixel(topDev, c, r)) continue;
      setPixel(topDev, c, r, false);
      // Platziere mittig oben in der unteren Matrix
      int order[] = {3, 4, 2, 5, 1, 6, 0, 7};
      for (int i = 0; i < 8; i++) {
        if (!getPixel(botDev, order[i], 0)) {
          setPixel(botDev, order[i], 0, true);
          return true;
        }
      }
      return true;
    }
  }
  return false;
}


// ═════════════════════════════════════════════════════════════════════════════
//  DISPLAY & POTENTIOMETER
// ═════════════════════════════════════════════════════════════════════════════

void showTime(long seconds) {
  if (seconds < 0) seconds = 0;
  int val = (int)(seconds / 60) * 100 + (int)(seconds % 60);
  tm.showNumberDecEx(val, 0b01000000, true);
}

void readPotentiometer() {
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(PIN_POT);
  setMinutes = (int)map(sum / 8, 0, 4095, 1, 99);
}


// ═════════════════════════════════════════════════════════════════════════════
//  BUZZER — non-blocking (aktiv: HIGH = AN)
// ═════════════════════════════════════════════════════════════════════════════

void startBeep(int times) {
  beepTotal  = times;
  beepDone   = 1;
  buzzerIsOn = true;
  digitalWrite(PIN_BUZZER, HIGH);
  buzzerUntil = millis() + BEEP_ON_MS;
}

void stopBuzzer() {
  beepTotal = 0;
  buzzerIsOn = false;
  digitalWrite(PIN_BUZZER, LOW);
}

void updateBuzzer() {
  if (beepTotal == 0) return;
  if (millis() < buzzerUntil) return;

  if (buzzerIsOn) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerIsOn = false;
    if (beepDone >= beepTotal) { beepTotal = 0; return; }
    buzzerUntil = millis() + BEEP_OFF_MS;
  } else {
    digitalWrite(PIN_BUZZER, HIGH);
    buzzerIsOn = true;
    beepDone++;
    buzzerUntil = millis() + BEEP_ON_MS;
  }
}


// ═════════════════════════════════════════════════════════════════════════════
//  ORIENTIERUNG — BNO085 via CodeCell
// ═════════════════════════════════════════════════════════════════════════════

void checkOrientation() {
  float ax, ay, az;
  myCodeCell.Motion_AccelerometerRead(ax, ay, az);

  if (ay > GRAVITY_THR) {
    topDev = MATRIX_TOP;
    botDev = MATRIX_BOTTOM;
  } else if (ay < -GRAVITY_THR) {
    topDev = MATRIX_BOTTOM;
    botDev = MATRIX_TOP;
  }
}


// ═════════════════════════════════════════════════════════════════════════════
//  START / RESET
// ═════════════════════════════════════════════════════════════════════════════

void startCountdown() {
  totalSec     = (long)setMinutes * 60L;
  remainSec    = totalSec;
  grainsMoved  = 0;
  grainInterval = (totalSec * 1000L) / MAX_GRAINS;
  if (grainInterval < 100) grainInterval = 100;

  clearAllGrids();
  fillGrid(topDev, MAX_GRAINS);

  grainNextMs   = millis() + grainInterval;
  displayNextMs = millis() + 1000;
  state = RUNNING;

  Serial.print("Start: "); Serial.print(setMinutes);
  Serial.print(" min | Korn alle "); Serial.print(grainInterval); Serial.println(" ms");
}

void resetToSetting() {
  state = SETTING;
  stopBuzzer();
  clearAllGrids();
  readPotentiometer();
  showTime((long)setMinutes * 60L);
  Serial.println("Reset → Einstellmodus");
}


// ═════════════════════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("=== Digitale Sanduhr ===");

  // CodeCell (BNO085)
  myCodeCell.Init(MOTION_ACCELEROMETER);

  // Pins
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  analogReadResolution(12);

  // TM1637
  tm.setBrightness(5);

  // MD_MAX72XX initialisieren
  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 4);  // Helligkeit 0–15
  mx.clear();
  memset(grid, 0, sizeof(grid));

  readPotentiometer();
  showTime((long)setMinutes * 60L);

  startBeep(1);
  Serial.println("Bereit. Taster drücken.");
}


// ═════════════════════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════════════════════

void loop() {

  // CodeCell Sensor-Update (muss jeden Loop aufgerufen werden)
  myCodeCell.Run(10);

  // Buzzer
  updateBuzzer();

  // ── Taster ────────────────────────────────────────────────────────────────
  bool btnDown = (digitalRead(PIN_BUTTON) == LOW);

  if (btnDown && !btnWasDown) {
    btnWasDown    = true;
    btnPressStart = millis();
  }

  if (!btnDown && btnWasDown) {
    btnWasDown = false;
    unsigned long held = millis() - btnPressStart;

    switch (state) {
      case SETTING:
        startCountdown();
        startBeep(2);
        break;
      case RUNNING:
        if (held >= LONG_PRESS_MS) {
          startBeep(1);
          resetToSetting();
        }
        break;
      case FINISHED:
        resetToSetting();
        break;
    }
  }

  // ── SETTING ───────────────────────────────────────────────────────────────
  if (state == SETTING) {
    readPotentiometer();
    showTime((long)setMinutes * 60L);
    delay(80);
    return;
  }

  // ── RUNNING ───────────────────────────────────────────────────────────────
  if (state == RUNNING) {
    checkOrientation();

    // Sand-Physik
    updateParticles(topDev);
    updateParticles(botDev);

    // Korn durch den Hals transferieren
    if (millis() >= grainNextMs && grainsMoved < MAX_GRAINS) {
      grainNextMs = millis() + grainInterval;
      if (transferGrain()) grainsMoved++;

      if (grainsMoved >= MAX_GRAINS) {
        state = FINISHED;
        remainSec     = 0;
        lastRepeatBeep = millis();
        showTime(0);
        startBeep(5);
        Serial.println("Zeit abgelaufen!");
        return;
      }
    }

    // Countdown-Anzeige
    if (millis() >= displayNextMs) {
      displayNextMs = millis() + 1000;
      if (remainSec > 0) remainSec--;
      showTime(remainSec);
    }

    delay(ANIM_DELAY_MS);
    return;
  }

  // ── FINISHED ──────────────────────────────────────────────────────────────
  if (state == FINISHED) {
    if (millis() - lastRepeatBeep > REPEAT_BEEP_MS) {
      lastRepeatBeep = millis();
      startBeep(2);
    }
    delay(100);
  }
}
