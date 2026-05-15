/*
 * ============================================================
 * Digitale Sanduhr  –  ESP32-C3
 * 2× MAX7219 (FC-16) - OHNE SENSOR
 * Exakt 36 Körner (Perfektes Dreieck) & Gleichmäßige Verteilung
 * ============================================================
 */

#include <MD_MAX72xx.h>
#include <TM1637Display.h>

// ── Pins & Hardware ───────────────────────────────────────────
#define HARDWARE_TYPE  MD_MAX72XX::FC16_HW
#define NUM_DEVICES    2
#define PIN_DIN        7
#define PIN_CLK        5
#define PIN_CS         6

#define PIN_POT        1   // Potentiometer
#define PIN_BUTTON     2   // Taster
#define PIN_BUZZER     3   // Passiver Piezo

#define PIN_TM_CLK     8
#define PIN_TM_DIO     9

// ── Projekt-Konstanten ────────────────────────────────────────
#define MAX_GRAINS      36     // EXAKT 36 KÖRNER (füllt das obere Dreieck perfekt)
#define LONG_PRESS_MS  2000    // Langer Druck = Abbruch
#define ANIM_STEP_MS    120    // Sanfte, langsame Animation

// ── Objekte ───────────────────────────────────────────────────
MD_MAX72XX    mx(HARDWARE_TYPE, PIN_DIN, PIN_CLK, PIN_CS, NUM_DEVICES);
TM1637Display tm(PIN_TM_CLK, PIN_TM_DIO);

// ── Zustände ──────────────────────────────────────────────────
enum State { SETTING, RUNNING, FINISHED };
State state = SETTING;

// ── Globale Variablen ─────────────────────────────────────────
bool  grid[2][8][8];          
const int topDev = 0;         // Festes oberes Display
const int botDev = 1;         // Festes unteres Display

int           setMinutes  = 5;
long          remainSec   = 0;
int           grainsMoved = 0;
unsigned long grainInterval;
unsigned long grainNextMs;
unsigned long displayNextMs;
unsigned long animNextMs;     
unsigned long btnPressStart;
bool          btnWasDown  = false;

// Buzzer-Variablen für das 5-malige Piepen
int           beepCount   = 0;
bool          buzzerState = false;
unsigned long lastBuzzerToggle = 0;

float smoothedPot = 0.0f;

// ─────────────────────────────────────────────────────────────
//  Matrix-Hilfsfunktionen (inkl. 180°-Drehung für topDev)
// ─────────────────────────────────────────────────────────────

void setPixel(int dev, int col, int row, bool on) {
  if (col < 0 || col > 7 || row < 0 || row > 7) return;
  
  int pCol = col;
  int pRow = row;
  
  // Dreht Hardware-Device 0 um 180°
  if (dev == topDev) {
    pCol = 7 - col;
    pRow = 7 - row;
  }
  
  grid[dev][pCol][pRow] = on;
  mx.setPoint(pRow, dev * 8 + pCol, on);
}

bool getPixel(int dev, int col, int row) {
  if (col < 0 || col > 7 || row < 0 || row > 7) return false;
  
  int pCol = col;
  int pRow = row;
  
  if (dev == topDev) {
    pCol = 7 - col;
    pRow = 7 - row;
  }
  
  return grid[dev][pCol][pRow];
}

void clearGrids() {
  mx.clear();
  memset(grid, 0, sizeof(grid));
}

// ─────────────────────────────────────────────────────────────
//  Initialbefüllung
// ─────────────────────────────────────────────────────────────

void fillTopGrid(int n) {
  int count = 0;
  for (int diag = 0; diag <= 7 && count < n; diag++) {
    for (int dc = 0; dc <= diag && count < n; dc++) {
      int c = 7 - dc;
      int r = diag - dc;
      if (c >= 0 && c <= 7 && r >= 0 && r <= 7) {
        setPixel(topDev, c, r, true);
        count++;
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  Sand-Physik: OBERES DISPLAY 
// ─────────────────────────────────────────────────────────────

void updateParticlesTop() {
  for (int r = 7; r >= 0; r--) {
    for (int c = 0; c <= 7; c++) {
      if (!getPixel(topDev, c, r)) continue;

      bool canDiag = (c > 0 && r < 7) && !getPixel(topDev, c - 1, r + 1);
      bool canLeft = (c > 0)           && !getPixel(topDev, c - 1, r);
      bool canDown = (r < 7)           && !getPixel(topDev, c,     r + 1);

      if (canDiag) {
        setPixel(topDev, c,     r,     false);
        setPixel(topDev, c - 1, r + 1, true);
      } else if (canLeft && canDown) {
        if (((millis() >> 4) + c + r) & 1) {
          setPixel(topDev, c,     r, false);
          setPixel(topDev, c - 1, r, true);
        } else {
          setPixel(topDev, c,     r,     false);
          setPixel(topDev, c,     r + 1, true);
        }
      } else if (canLeft) {
        setPixel(topDev, c,     r, false);
        setPixel(topDev, c - 1, r, true);
      } else if (canDown) {
        setPixel(topDev, c,     r,     false);
        setPixel(topDev, c,     r + 1, true);
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  Sand-Physik: UNTERES DISPLAY 
// ─────────────────────────────────────────────────────────────

void updateParticlesBot() {
  for (int r = 7; r >= 0; r--) {
    for (int c = 7; c >= 0; c--) {
      if (!getPixel(botDev, c, r)) continue;

      bool canDiag  = (c < 7 && r < 7) && !getPixel(botDev, c + 1, r + 1);
      bool canRight = (c < 7)           && !getPixel(botDev, c + 1, r);
      bool canDown  = (r < 7)           && !getPixel(botDev, c,     r + 1);

      if (canDiag) {
        setPixel(botDev, c,     r,     false);
        setPixel(botDev, c + 1, r + 1, true);
      } else if (canRight && canDown) {
        if (((millis() >> 4) + c + r) & 1) {
          setPixel(botDev, c,     r, false);
          setPixel(botDev, c + 1, r, true);
        } else {
          setPixel(botDev, c,     r,     false);
          setPixel(botDev, c,     r + 1, true);
        }
      } else if (canRight) {
        setPixel(botDev, c,     r, false);
        setPixel(botDev, c + 1, r, true);
      } else if (canDown) {
        setPixel(botDev, c,     r,     false);
        setPixel(botDev, c,     r + 1, true);
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  Transfer: Korn von oben nach unten übergeben
// ─────────────────────────────────────────────────────────────

bool transferGrain() {
  int bestC = -1, bestR = -1, bestDist = 999;
  
  for (int r = 0; r <= 7; r++) {
    for (int c = 0; c <= 7; c++) {
      if (!getPixel(topDev, c, r)) continue;

      int dist = c + (7 - r);
      if (dist < bestDist) {
        bestDist = dist;
        bestC = c; bestR = r;
      }
    }
  }

  if (bestC == -1 || bestDist > 3) return false;

  int spawnC = -1, spawnR = -1;
  for (int sum = 0; sum <= 4; sum++) {
    for (int cc = 0; cc <= sum; cc++) {
      int rr = sum - cc;
      if (rr > 7 || cc > 7) continue;
      if (!getPixel(botDev, cc, rr)) {
        spawnC = cc;
        spawnR = rr;
        break;
      }
    }
    if (spawnC != -1) break;
  }

  if (spawnC == -1) return false;

  setPixel(topDev, bestC, bestR, false);
  setPixel(botDev, spawnC, spawnR, true);
  
  return true;
}

// ─────────────────────────────────────────────────────────────
//  Hilfsfunktionen
// ─────────────────────────────────────────────────────────────

void showTime(long seconds) {
  int m = seconds / 60;
  int s = seconds % 60;
  tm.showNumberDecEx((m * 100) + s, 0b01000000, true);
}

// ─────────────────────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  analogReadResolution(12);

  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 2);

  tm.setBrightness(5);
  clearGrids();

  smoothedPot = (float)analogRead(PIN_POT);
  showTime((long)setMinutes * 60);
}

// ─────────────────────────────────────────────────────────────
//  Hauptschleife
// ─────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();
  bool btnDown = (digitalRead(PIN_BUTTON) == LOW);

  // --- Taster-Steuerung ---
  if (btnDown && !btnWasDown) {
    btnWasDown = true;
    btnPressStart = now;
  }

  if (!btnDown && btnWasDown) {
    btnWasDown = false;
    unsigned long duration = now - btnPressStart;

    if (state == SETTING) {
      // --- START COUNTDOWN ---
      tone(PIN_BUZZER, 1500, 100);
      
      remainSec = (long)setMinutes * 60;
      // Teile die Zeit exakt auf die 36 Sandkörner auf
      grainInterval = ((unsigned long)remainSec * 1000UL) / MAX_GRAINS; 
      
      grainsMoved = 0;
      clearGrids();
      fillTopGrid(MAX_GRAINS);
      
      displayNextMs = millis() + 1000UL;
      animNextMs    = millis() + ANIM_STEP_MS;
      grainNextMs   = millis() + grainInterval;
      state = RUNNING;

    } else if (state == RUNNING && duration >= LONG_PRESS_MS) {
      // --- ABBRUCH DURCH LANGEN DRUCK ---
      tone(PIN_BUZZER, 800, 400); 
      noTone(PIN_BUZZER);
      state = SETTING;
      showTime((long)setMinutes * 60);
      
    } else if (state == FINISHED) {
      // --- ZURÜCK ZUM EINSTELLEN ---
      noTone(PIN_BUZZER);
      state = SETTING;
      showTime((long)setMinutes * 60);
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
    
    // 1. Physik updaten (flüssige, langsame Animation)
    if (now >= animNextMs) {
      animNextMs = now + ANIM_STEP_MS;
      updateParticlesTop();
      updateParticlesBot();
    }

    // 2. Sand-Transfer (Absolut gleichmäßig)
    if (now >= grainNextMs && grainsMoved < MAX_GRAINS) {
      if (transferGrain()) {
        grainsMoved++;
        // Warte das exakte Intervall ab, damit jedes der 36 Körner exakt getaktet ist
        grainNextMs = millis() + grainInterval;
      }
    }

    // 3. Display-Update (Sekundentick)
    if (now >= displayNextMs) {
      displayNextMs += 1000UL;
      
      if (remainSec > 0) {
        remainSec--;
        showTime(remainSec);
      } 
      
      if (remainSec <= 0) {
        state = FINISHED;
        beepCount = 0;            
        buzzerState = false;
        lastBuzzerToggle = millis();
      }
    }
  }
  
  // --- Modus: Beendet ---
  else if (state == FINISHED) {
    // Restliche Animation zu Ende laufen lassen
    if (now >= animNextMs) {
      animNextMs = now + ANIM_STEP_MS;
      updateParticlesTop();
      updateParticlesBot();
    }
    
    tm.showNumberDecEx(0, 0b01000000, true);
    
    // Exakt 5 Beeps abspielen und dann aufhören
    if (beepCount < 5) {
      if (now - lastBuzzerToggle >= 500) {
        lastBuzzerToggle = now;
        buzzerState = !buzzerState;
        
        if (buzzerState) {
          tone(PIN_BUZZER, 2000);
        } else {
          noTone(PIN_BUZZER);
          beepCount++; 
        }
      }
    } else {
      noTone(PIN_BUZZER); 
    }
  }
}