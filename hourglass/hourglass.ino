/*
 * ============================================================
 *  Digitale Sanduhr — ESP32-C3 Mini (CodeCell)
 * ============================================================
 *
 *  BAUTEILE
 *  ─────────────────────────────────────────────────────────
 *  • CodeCell ESP32-C3 Mini 1
 *      – eingebauter Motion-Sensor BNO085 (I2C, intern)
 *  • 2× 8×8 LED-Matrix mit MAX7219 (daisy-chained)
 *  • TM1637 4-Digit 7-Segment Display (MM:SS)
 *  • Potentiometer (3-Pin)
 *  • Taster (2-Pin, Momentary)
 *  • Aktives Buzzer-Modul (3-Pin: VCC · GND · I/O)
 *
 *  VERDRAHTUNG
 *  ─────────────────────────────────────────────────────────
 *  LED-Matrizen (daisy-chained):
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
 *  Potentiometer:
 *    Pin 1 (links)  → 3.3V
 *    Pin 2 (Mitte)  → G1   (ADC)
 *    Pin 3 (rechts) → GND
 *
 *  Taster:
 *    Pin A → G2
 *    Pin B → GND
 *    (interner Pull-up aktiv, kein externer Widerstand nötig)
 *
 *  Buzzer-Modul (aktiv, 3-Pin):
 *    VCC → 3.3V
 *    GND → GND
 *    I/O → G3
 *    (HIGH = Ton AN, LOW = Ton AUS)
 *
 *  FUNKTIONSWEISE
 *  ─────────────────────────────────────────────────────────
 *  • Potentiometer stellt Minuten ein (1–99)
 *  • 4-Digit Display zeigt MM:SS
 *  • Taster kurz drücken  → Countdown starten
 *  • Taster ≥2s halten    → Countdown abbrechen
 *  • Sand-Animation auf den LED-Matrizen
 *  • BNO085 erkennt Umdrehung → Sanduhr dreht sich um
 *  • Nach Ablauf: Buzzer-Signal, dann Taster drücken → Reset
 * ============================================================
 */

#include "LedControl.h"
#include "Delay.h"
#include <CodeCell.h>        // CodeCell-Bibliothek (BNO085)
#include <TM1637Display.h>   // 4-Digit 7-Segment Display

// ── Pin-Definitionen ──────────────────────────────────────────────────────────
#define PIN_DIN     7    // MAX7219 Data
#define PIN_CLK     5    // MAX7219 Clock
#define PIN_CS      6    // MAX7219 Chip-Select

#define PIN_POT     1    // Potentiometer (ADC, 0–3.3V)
#define PIN_BUTTON  2    // Taster (aktiv LOW, Pull-up intern)
#define PIN_BUZZER  3    // Aktives Buzzer-Modul I/O (HIGH = AN)

#define PIN_TM_CLK  8    // TM1637 CLK
#define PIN_TM_DIO  9    // TM1637 DIO

// ── Konstanten ────────────────────────────────────────────────────────────────
#define MATRIX_TOP_DEFAULT     0     // Adresse obere Matrix beim Start
#define MATRIX_BOTTOM_DEFAULT  1     // Adresse untere Matrix beim Start
#define MAX_GRAINS             60    // Sandkörner gesamt
#define GRAVITY_THRESHOLD      4.5f  // m/s² – Schwellenwert Umdrehungserkennung
#define LONG_PRESS_MS          2000  // ms für langen Tasterdruck (Abbruch)
#define ANIM_DELAY_MS          30    // ms pro Animations-Frame
#define BEEP_ON_MS             150   // ms Buzzer AN pro Piep
#define BEEP_OFF_MS            150   // ms Pause zwischen Pieptönen
#define REPEAT_BEEP_INTERVAL   5000  // ms zwischen Wiederholungs-Beeps im FINISHED

// ── Objekte ───────────────────────────────────────────────────────────────────
LedControl    lc(PIN_DIN, PIN_CLK, PIN_CS, 2);
NonBlockDelay grainTimer;
NonBlockDelay displayTimer;
NonBlockDelay buzzerTimer;
CodeCell      myCodeCell;
TM1637Display tm(PIN_TM_CLK, PIN_TM_DIO);

// ── Zustands-Maschine ─────────────────────────────────────────────────────────
enum State { SETTING, RUNNING, FINISHED };
State state = SETTING;

// ── Variablen ─────────────────────────────────────────────────────────────────
int  topMatrix     = MATRIX_TOP_DEFAULT;
int  bottomMatrix  = MATRIX_BOTTOM_DEFAULT;
int  setMinutes    = 5;
long totalSeconds  = 0;
long remainSeconds = 0;
int  grainsMoved   = 0;
long grainInterval = 1000;

bool          btnWasDown     = false;
unsigned long btnPressStart  = 0;

int           beepTotal      = 0;
int           beepDone       = 0;
bool          buzzerIsOn     = false;
unsigned long lastRepeatBeep = 0;


// ═════════════════════════════════════════════════════════════════════════════
//  BUZZER — non-blocking (aktives Modul: HIGH = AN)
// ═════════════════════════════════════════════════════════════════════════════

void startBeep(int times) {
  beepTotal  = times;
  beepDone   = 1;
  buzzerIsOn = true;
  digitalWrite(PIN_BUZZER, HIGH);
  buzzerTimer.Delay(BEEP_ON_MS);
}

void updateBuzzer() {
  if (beepTotal == 0) return;
  if (!buzzerTimer.Timeout()) return;

  if (buzzerIsOn) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerIsOn = false;
    if (beepDone >= beepTotal) {
      beepTotal = 0;
      return;
    }
    buzzerTimer.Delay(BEEP_OFF_MS);
  } else {
    digitalWrite(PIN_BUZZER, HIGH);
    buzzerIsOn = true;
    beepDone++;
    buzzerTimer.Delay(BEEP_ON_MS);
  }
}

void stopBuzzer() {
  beepTotal  = 0;
  beepDone   = 0;
  buzzerIsOn = false;
  digitalWrite(PIN_BUZZER, LOW);
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
//  LED-MATRIX
// ═════════════════════════════════════════════════════════════════════════════

void fillMatrix(int addr, int grains) {
  lc.clearDisplay(addr);
  int count = 0;
  for (int y = 0; y < 8 && count < grains; y++)
    for (int x = 0; x < 8 && count < grains; x++) {
      lc.setRawXY(addr, x, y, true);
      count++;
    }
}

// Sand-Physik: Körner fallen nach unten, weichen nach links/rechts aus
void updateParticles(int addr) {
  for (int y = 6; y >= 0; y--) {
    for (int x = 0; x < 8; x++) {
      if (!lc.getRawXY(addr, x, y)) continue;

      bool canDown  = !lc.getRawXY(addr, x, y + 1);
      bool canLeft  = (x > 0) && !lc.getRawXY(addr, x - 1, y + 1);
      bool canRight = (x < 7) && !lc.getRawXY(addr, x + 1, y + 1);

      if (canDown) {
        lc.setRawXY(addr, x, y,     false);
        lc.setRawXY(addr, x, y + 1, true);
      } else if (canLeft && !canRight) {
        lc.setRawXY(addr, x,     y,     false);
        lc.setRawXY(addr, x - 1, y + 1, true);
      } else if (canRight && !canLeft) {
        lc.setRawXY(addr, x,     y,     false);
        lc.setRawXY(addr, x + 1, y + 1, true);
      } else if (canLeft && canRight) {
        // Beide Seiten frei → zufällig wählen für natürliches Verhalten
        bool goRight = ((millis() / 10 + x + y) % 2 == 0);
        lc.setRawXY(addr, x,              y,     false);
        lc.setRawXY(addr, goRight ? x+1 : x-1, y + 1, true);
      }
    }
  }
}

// Überträgt ein Korn aus der oberen in die untere Matrix (Hals)
bool transferGrain() {
  for (int y = 7; y >= 0; y--) {
    for (int x = 7; x >= 0; x--) {
      if (!lc.getRawXY(topMatrix, x, y)) continue;
      lc.setRawXY(topMatrix, x, y, false);
      // In Mitte oben der unteren Matrix einlegen
      int startX[] = {3, 4, 2, 5, 1, 6, 0, 7};
      for (int i = 0; i < 8; i++) {
        if (!lc.getRawXY(bottomMatrix, startX[i], 0)) {
          lc.setRawXY(bottomMatrix, startX[i], 0, true);
          return true;
        }
      }
      return true;
    }
  }
  return false;
}


// ═════════════════════════════════════════════════════════════════════════════
//  ORIENTIERUNG (BNO085)
// ═════════════════════════════════════════════════════════════════════════════

void checkOrientation() {
  float ax, ay, az;
  myCodeCell.Motion_AccelerometerRead(ax, ay, az);  // Korrekter CodeCell API-Name

  if (ay > GRAVITY_THRESHOLD) {
    topMatrix    = MATRIX_TOP_DEFAULT;
    bottomMatrix = MATRIX_BOTTOM_DEFAULT;
    lc.setRotation(0);
  } else if (ay < -GRAVITY_THRESHOLD) {
    topMatrix    = MATRIX_BOTTOM_DEFAULT;
    bottomMatrix = MATRIX_TOP_DEFAULT;
    lc.setRotation(180);
  }
}


// ═════════════════════════════════════════════════════════════════════════════
//  START / RESET
// ═════════════════════════════════════════════════════════════════════════════

void startCountdown() {
  totalSeconds  = (long)setMinutes * 60L;
  remainSeconds = totalSeconds;
  grainsMoved   = 0;
  grainInterval = (totalSeconds * 1000L) / MAX_GRAINS;
  if (grainInterval < 100) grainInterval = 100;

  lc.clearDisplay(0);
  lc.clearDisplay(1);
  fillMatrix(topMatrix, MAX_GRAINS);

  grainTimer.Delay(grainInterval);
  displayTimer.Delay(1000);
  state = RUNNING;

  Serial.print("Start: ");
  Serial.print(setMinutes);
  Serial.print(" min | Korn-Takt: ");
  Serial.print(grainInterval);
  Serial.println(" ms");
}

void resetToSetting() {
  state = SETTING;
  stopBuzzer();
  lc.clearDisplay(0);
  lc.clearDisplay(1);
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

  myCodeCell.Init(MOTION_ACCELEROMETER);

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  analogReadResolution(12);

  tm.setBrightness(5);

  for (int i = 0; i < 2; i++) {
    lc.shutdown(i, false);
    lc.setIntensity(i, 4);
    lc.clearDisplay(i);
  }
  lc.setRotation(0);

  readPotentiometer();
  showTime((long)setMinutes * 60L);

  startBeep(1);   // Startton
  Serial.println("Bereit.");
}


// ═════════════════════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════════════════════

void loop() {

  // CodeCell Run: liest Sensordaten (muss jeden Loop aufgerufen werden)
  myCodeCell.Run(10);  // 10 Hz Sensor-Abtastrate

  // Non-blocking Buzzer
  updateBuzzer();

  // ── Taster ────────────────────────────────────────────────────────────────
  bool btnDown = (digitalRead(PIN_BUTTON) == LOW);

  if (btnDown && !btnWasDown) {
    btnWasDown   = true;
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
    updateParticles(topMatrix);
    updateParticles(bottomMatrix);

    if (grainTimer.Timeout() && grainsMoved < MAX_GRAINS) {
      grainTimer.Delay(grainInterval);
      if (transferGrain()) grainsMoved++;

      if (grainsMoved >= MAX_GRAINS) {
        state = FINISHED;
        remainSeconds  = 0;
        lastRepeatBeep = millis();
        showTime(0);
        startBeep(5);
        Serial.println("Zeit abgelaufen!");
        return;
      }
    }

    if (displayTimer.Timeout()) {
      displayTimer.Delay(1000);
      if (remainSeconds > 0) remainSeconds--;
      showTime(remainSeconds);
    }

    delay(ANIM_DELAY_MS);
    return;
  }

  // ── FINISHED ──────────────────────────────────────────────────────────────
  if (state == FINISHED) {
    if (millis() - lastRepeatBeep > REPEAT_BEEP_INTERVAL) {
      lastRepeatBeep = millis();
      startBeep(2);
    }
    delay(100);
  }
}
