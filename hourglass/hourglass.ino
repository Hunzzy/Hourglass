/*
 * ============================================================
 * Digitale Sanduhr — Finale Labor-Version (ESP32-C3)
 * DIAGONALE PHYSIK (für 45° Drehung) | Stabilisiertes Poti
 * ============================================================
 */

#include <MD_MAX72xx.h>
#include <TM1637Display.h>

// --- Hardware-Pins (laut CodeCell Pinout) ---
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW 
#define NUM_DEVICES   2                   
#define PIN_DIN       7
#define PIN_CLK       5
#define PIN_CS        6

#define PIN_POT       1    // Potentiometer an G1 [cite: 8]
#define PIN_BUTTON    2    // Taster an G2 [cite: 8]
#define PIN_BUZZER    3    // Passiver Buzzer an G3 [cite: 8, 11]

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

// Poti-Filter (Exponential Moving Average)
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

// Füllt die obere Matrix von der "oberen Spitze" (0,0) aus
void fillGrid(int dev, int n) {
  int count = 0;
  for (int r = 0; r < 8 && count < n; r++) {
    for (int c = 0; c < 8 && count < n; c++) {
      setPixel(dev, c, r, true);
      count++;
    }
  }
}

// --- DIAGONALE SAND-PHYSIK ---
// Berechnet die Bewegung in Richtung der unteren Ecke (7,7)
void updateParticles(int dev) {
  // Wir scannen von der unteren Ecke (7,7) rückwärts nach (0,0)
  for (int r = 7; r >= 0; r--) {
    for (int c = 7; c >= 0; c--) {
      if (!getPixel(dev, c, r)) continue;

      // Ziel: Pixel (r+1, c+1) - das ist die absolute Spitze unten
      bool canMoveDiag  = (r < 7 && c < 7) && !getPixel(dev, c + 1, r + 1);
      bool canMoveDown  = (r < 7) && !getPixel(dev, c, r + 1);
      bool canMoveRight = (c < 7) && !getPixel(dev, c + 1, r);

      if (canMoveDiag) {
        setPixel(dev, c, r, false);
        setPixel(dev, c + 1, r + 1, true);
      } else if (canMoveDown && !canMoveRight) {
        setPixel(dev, c, r, false);
        setPixel(dev, c, r + 1, true);
      } else if (canMoveRight && !canMoveDown) {
        setPixel(dev, c, r, false);
        setPixel(dev, c + 1, r, true);
      } else if (canMoveDown && canMoveRight) {
        // Zufällig links oder rechts abrutschen
        if (millis() % 2 == 0) {
          setPixel(dev, c, r, false);
          setPixel(dev, c, r + 1, true);
        } else {
          setPixel(dev, c, r, false);
          setPixel(dev, c + 1, r, true);
        }
      }
    }
  }
}

// Transferiert ein Korn von der unteren Spitze (7,7) zur oberen Spitze (0,0)
bool transferGrain() {
  if (getPixel(topDev, 7, 7)) {
    setPixel(topDev, 7, 7, false);
    // In der oberen Ecke der unteren Matrix einfügen
    if (!getPixel(botDev, 0, 0)) {
      setPixel(botDev, 0, 0, true);
      return true;
    }
    // Falls (0,0) blockiert, Nachbarn probieren
    else if (!getPixel(botDev, 1, 0)) { setPixel(botDev, 1, 0, true); return true; }
    else if (!getPixel(botDev, 0, 1)) { setPixel(botDev, 0, 1, true); return true; }
  }
  return false;
}

void showTime(long seconds) {
  int m = seconds / 60;
  int s = seconds % 60;
  tm.showNumberDecEx((m * 100) + s, 0b01000000, true); 
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
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
      tone(PIN_BUZZER, 1500, 100); // Start-Ton 
      remainSec = (long)setMinutes * 60;
      grainInterval = (remainSec * 1000) / MAX_GRAINS;
      grainsMoved = 0;
      clearGrids();
      fillGrid(topDev, MAX_GRAINS);
      displayNextMs = millis() + 1000;
      grainNextMs = millis() + grainInterval;
      state = RUNNING;
    } 
    else if (state == RUNNING && duration >= LONG_PRESS_MS) {
      tone(PIN_BUZZER, 800, 400); // Abbruch-Ton 
      state = SETTING;
    } 
    else if (state == FINISHED) {
      noTone(PIN_BUZZER);
      state = SETTING;
    }
  }

  // --- Modus: Einstellen ---
  if (state == SETTING) {
    int rawPot = analogRead(PIN_POT);
    smoothedPot = (smoothedPot * 0.92) + (rawPot * 0.08); // Filter für Stabilität 
    
    int newMinutes = map((int)smoothedPot, 0, 4095, 1, 99);
    if (newMinutes != setMinutes) {
      setMinutes = newMinutes;
      showTime((long)setMinutes * 60); 
    }
    delay(20);
  } 
  
  // --- Modus: Countdown ---
  else if (state == RUNNING) {
    updateParticles(topDev);
    updateParticles(botDev);

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
        state = FINISHED; // Zeit abgelaufen 
      }
    }
    delay(ANIM_DELAY_MS);
  }
  
  // --- Modus: Beendet ---
  else if (state == FINISHED) {
    if ((millis() / 500) % 2 == 0) tone(PIN_BUZZER, 2000); 
    else noTone(PIN_BUZZER);
  }
}