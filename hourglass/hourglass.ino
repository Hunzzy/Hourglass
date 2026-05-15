/*
 * ============================================================
 * Digitale Sanduhr — Korrigierte Version (ESP32-C3)
 * 
 * FIXES:
 *  1. Letztes Sandkorn bleibt nicht mehr hängen — Transfer-Logik
 *     sucht aktiv nach dem Korn, das der Engstelle (col 7) am
 *     nächsten ist, statt nur exakt (7,7) zu prüfen.
 *  2. Buzzer piept nicht mehr nach Programmende — noTone() wird
 *     beim Wechsel in SETTING zuverlässig aufgerufen.
 *  3. Sand fällt jetzt wie in echter Sanduhr:
 *     - Oberes Display (um 45° gedreht): Körner sammeln sich
 *       in der unteren rechten Ecke (= Engstelle der Sanduhr)
 *       und fallen von dort zur Diagonalen hin.
 *     - Physik: Hauptbewegung diagonal zur Ecke (7,7), dann
 *       entlang der Diagonalen abrutschen — sieht aus wie
 *       Sand der durch eine enge Öffnung fällt.
 * ============================================================
 */

#include <MD_MAX72xx.h>
#include <TM1637Display.h>

// --- Hardware-Pins ---
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define NUM_DEVICES   2
#define PIN_DIN       7
#define PIN_CLK       5
#define PIN_CS        6

#define PIN_POT       1
#define PIN_BUTTON    2
#define PIN_BUZZER    3

#define PIN_TM_CLK    8
#define PIN_TM_DIO    9

// --- Projekt-Parameter ---
#define MAX_GRAINS    60
#define LONG_PRESS_MS 2000
#define ANIM_DELAY_MS 40

// --- Objekte ---
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, PIN_DIN, PIN_CLK, PIN_CS, NUM_DEVICES);
TM1637Display tm(PIN_TM_CLK, PIN_TM_DIO);

// --- Zustände ---
enum State { SETTING, RUNNING, FINISHED };
State state = SETTING;

// --- Globale Variablen ---
bool grid[2][8][8];
int topDev = 0;
int botDev = 1;

int setMinutes = 5;
long remainSec = 0;
int grainsMoved = 0;
unsigned long grainInterval, grainNextMs, displayNextMs, btnPressStart;
bool btnWasDown = false;
bool buzzerActive = false;  // FIX: Buzzer-Status verfolgen

float smoothedPot = 0;

// --- Matrix-Hilfsfunktionen ---
void setPixel(int dev, int col, int row, bool on) {
  if (col < 0 || col > 7 || row < 0 || row > 7) return;
  grid[dev][col][row] = on;
  mx.setPoint(row, dev * 8 + col, on);
}

bool getPixel(int dev, int col, int row) {
  if (col < 0 || col > 7 || row < 0 || row > 7) return false;
  return grid[dev][col][row];
}

void clearGrids() {
  mx.clear();
  memset(grid, 0, sizeof(grid));
}

// ---------------------------------------------------------------
// Füllt das obere Display dreieckig von der oberen LINKEN Ecke
// (0,0) aus — bei 45°-Drehung ist das die obere Spitze der Sanduhr.
// Die Körner füllen das obere Dreieck: Diagonale von (0,7)→(7,0)
// und darüber.
// ---------------------------------------------------------------
void fillTopGrid(int n) {
  int count = 0;
  // Diagonal von oben links füllen: Zeilen-Diagonale
  // Bei 45° Drehung: obere Hälfte = Dreieck wo col+row <= 7
  for (int diag = 0; diag <= 7 && count < n; diag++) {
    for (int c = 0; c <= diag && count < n; c++) {
      int r = diag - c;
      setPixel(topDev, c, r, true);
      count++;
    }
  }
}

// ---------------------------------------------------------------
// Füllt das untere Display dreieckig von der unteren RECHTEN Ecke
// (7,7) aus — bei 45°-Drehung ist das die untere Spitze.
// Körner füllen das untere Dreieck: wo col+row >= 7
// ---------------------------------------------------------------
void fillBotGrid(int n) {
  int count = 0;
  for (int diag = 14; diag >= 7 && count < n; diag--) {
    for (int c = 7; c >= 0 && count < n; c--) {
      int r = diag - c;
      if (r < 0 || r > 7) continue;
      setPixel(botDev, c, r, true);
      count++;
    }
  }
}

// ---------------------------------------------------------------
// SAND-PHYSIK — Oberes Display
// Das Display ist um 45° gedreht. Die Engstelle der Sanduhr
// liegt bei Ecke (col=7, row=7). Sand fällt diagonal zur Ecke.
// Hauptbewegung: in Richtung (c+1, r+1) = zur Ecke (7,7).
// Abrutschen: entlang der Diagonalen (c+1, r) oder (c, r+1).
// ---------------------------------------------------------------
void updateParticlesTop() {
  // Scan von (7,7) rückwärts damit Körner nicht doppelt bewegt werden
  for (int r = 7; r >= 0; r--) {
    for (int c = 7; c >= 0; c--) {
      if (!getPixel(topDev, c, r)) continue;

      // Hauptrichtung: diagonal zur Engstelle (7,7)
      bool canDiag  = (c < 7 && r < 7) && !getPixel(topDev, c + 1, r + 1);
      // Abrutschen entlang der Diagonale
      bool canRight = (c < 7) && !getPixel(topDev, c + 1, r);
      bool canDown  = (r < 7) && !getPixel(topDev, c, r + 1);

      if (canDiag) {
        setPixel(topDev, c, r, false);
        setPixel(topDev, c + 1, r + 1, true);
      } else if (canRight && canDown) {
        // Zufällig eine Seite wählen (Sand verteilt sich natürlich)
        if ((millis() / 13 + c + r) % 2 == 0) {
          setPixel(topDev, c, r, false);
          setPixel(topDev, c + 1, r, true);
        } else {
          setPixel(topDev, c, r, false);
          setPixel(topDev, c, r + 1, true);
        }
      } else if (canRight) {
        setPixel(topDev, c, r, false);
        setPixel(topDev, c + 1, r, true);
      } else if (canDown) {
        setPixel(topDev, c, r, false);
        setPixel(topDev, c, r + 1, true);
      }
    }
  }
}

// ---------------------------------------------------------------
// SAND-PHYSIK — Unteres Display
// Körner kommen bei (0,0) an und fallen diagonal zur Ecke (7,7).
// Gleiche Logik wie oben.
// ---------------------------------------------------------------
void updateParticlesBot() {
  // Scan von (7,7) rückwärts
  for (int r = 7; r >= 0; r--) {
    for (int c = 7; c >= 0; c--) {
      if (!getPixel(botDev, c, r)) continue;

      bool canDiag  = (c < 7 && r < 7) && !getPixel(botDev, c + 1, r + 1);
      bool canRight = (c < 7) && !getPixel(botDev, c + 1, r);
      bool canDown  = (r < 7) && !getPixel(botDev, c, r + 1);

      if (canDiag) {
        setPixel(botDev, c, r, false);
        setPixel(botDev, c + 1, r + 1, true);
      } else if (canRight && canDown) {
        if ((millis() / 13 + c + r) % 2 == 0) {
          setPixel(botDev, c, r, false);
          setPixel(botDev, c + 1, r, true);
        } else {
          setPixel(botDev, c, r, false);
          setPixel(botDev, c, r + 1, true);
        }
      } else if (canRight) {
        setPixel(botDev, c, r, false);
        setPixel(botDev, c + 1, r, true);
      } else if (canDown) {
        setPixel(botDev, c, r, false);
        setPixel(botDev, c, r + 1, true);
      }
    }
  }
}

// ---------------------------------------------------------------
// TRANSFER: Korn von der Engstelle oben (nah an col=7, row=7)
// zur Engstelle unten (col=0, row=0) übertragen.
//
// FIX: Sucht das Korn mit dem größten (col+row) Wert — das ist
// das Korn das der Engstelle am nächsten ist. So bleibt kein
// Korn hängen, auch wenn es nie exakt (7,7) erreicht.
// ---------------------------------------------------------------
bool transferGrain() {
  // Suche das Korn mit größtem Diagonalwert (c+r = nah an (7,7))
  int bestC = -1, bestR = -1, bestSum = -1;
  for (int r = 7; r >= 0; r--) {
    for (int c = 7; c >= 0; c--) {
      if (getPixel(topDev, c, r)) {
        int s = c + r;
        if (s > bestSum) {
          bestSum = s;
          bestC = c;
          bestR = r;
        }
      }
    }
  }

  if (bestC == -1) return false; // Keine Körner mehr oben

  // Korn nur übertragen wenn es wirklich in der Nähe der Engstelle ist
  // (Diagonalsumme >= 12, also z.B. (5,7), (6,6), (7,5) …)
  if (bestSum < 12) return false;

  // Korn von oberer Matrix entfernen
  setPixel(topDev, bestC, bestR, false);

  // In der unteren Matrix bei (0,0) einfügen — Engstelle unten
  if (!getPixel(botDev, 0, 0)) {
    setPixel(botDev, 0, 0, true);
  } else if (!getPixel(botDev, 1, 0)) {
    setPixel(botDev, 1, 0, true);
  } else if (!getPixel(botDev, 0, 1)) {
    setPixel(botDev, 0, 1, true);
  } else {
    // Notfall: (0,0) ist blockiert, finde nächsten freien Platz
    bool placed = false;
    for (int sum = 0; sum <= 2 && !placed; sum++) {
      for (int cc = 0; cc <= sum && !placed; cc++) {
        int rr = sum - cc;
        if (!getPixel(botDev, cc, rr)) {
          setPixel(botDev, cc, rr, true);
          placed = true;
        }
      }
    }
  }
  return true;
}

void showTime(long seconds) {
  int m = seconds / 60;
  int s = seconds % 60;
  tm.showNumberDecEx((m * 100) + s, 0b01000000, true);
}

// FIX: Buzzer sicher stoppen und State wechseln
void goToSetting() {
  noTone(PIN_BUZZER);
  buzzerActive = false;
  state = SETTING;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW); // Buzzer explizit aus beim Start
  analogReadResolution(12);

  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 2);
  tm.setBrightness(5);

  clearGrids();
  smoothedPot = analogRead(PIN_POT);
}

void loop() {
  bool btnDown = (digitalRead(PIN_BUTTON) == LOW);

  // --- Taster-Steuerung ---
  if (btnDown && !btnWasDown) {
    btnWasDown = true;
    btnPressStart = millis();
  }

  if (!btnDown && btnWasDown) {
    btnWasDown = false;
    unsigned long duration = millis() - btnPressStart;

    if (state == SETTING) {
      tone(PIN_BUZZER, 1500, 100);
      remainSec = (long)setMinutes * 60;
      grainInterval = (remainSec * 1000UL) / MAX_GRAINS;
      grainsMoved = 0;
      clearGrids();
      fillTopGrid(MAX_GRAINS);
      displayNextMs = millis() + 1000;
      grainNextMs = millis() + grainInterval;
      state = RUNNING;
    }
    else if (state == RUNNING && duration >= LONG_PRESS_MS) {
      tone(PIN_BUZZER, 800, 400);
      goToSetting();
    }
    else if (state == FINISHED) {
      goToSetting(); // FIX: Buzzer wird hier zuverlässig gestoppt
    }
  }

  // --- Modus: Einstellen ---
  if (state == SETTING) {
    int rawPot = analogRead(PIN_POT);
    smoothedPot = (smoothedPot * 0.92) + (rawPot * 0.08);

    int newMinutes = map((int)smoothedPot, 0, 4095, 1, 99);
    if (newMinutes != setMinutes) {
      setMinutes = newMinutes;
      showTime((long)setMinutes * 60);
    }
    delay(20);
  }

  // --- Modus: Countdown ---
  else if (state == RUNNING) {
    updateParticlesTop();
    updateParticlesBot();

    if (millis() >= grainNextMs && grainsMoved < MAX_GRAINS) {
      if (transferGrain()) {
        grainsMoved++;
        grainNextMs = millis() + grainInterval;
      }
    }

    if (millis() >= displayNextMs) {
      displayNextMs += 1000;
      if (remainSec > 0) {
        remainSec--;
        showTime(remainSec);
      } else {
        state = FINISHED;
      }
    }
    delay(ANIM_DELAY_MS);
  }

  // --- Modus: Beendet ---
  else if (state == FINISHED) {
    // FIX: Buzzer-Status mit Flag verwalten — kein random piepen mehr
    unsigned long t = millis() / 500;
    bool shouldBeep = (t % 2 == 0);
    if (shouldBeep && !buzzerActive) {
      tone(PIN_BUZZER, 2000);
      buzzerActive = true;
    } else if (!shouldBeep && buzzerActive) {
      noTone(PIN_BUZZER);
      buzzerActive = false;
    }
  }
}
