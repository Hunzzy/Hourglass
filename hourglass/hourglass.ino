/*
 * ============================================================
 * Digitale Sanduhr — ESP32-C3 Mini (CodeCell)
 * Nutzung von Standard-Bibliotheken (KEINE lokalen Files)
 * ============================================================
 */

#include <MD_MAX72xx.h>
#include <TM1637Display.h>
#include <CodeCell.h>

// --- Hardware-Konfiguration ---
#define HARDWARE_TYPE MD_MAX72XX::GENERIC_HW // Falls gespiegelt: GENERIC_HW probieren
#define NUM_DEVICES   2                   // 2 Matrizen daisy-chained
#define PIN_DIN       7
#define PIN_CLK       5
#define PIN_CS        6

#define PIN_POT       1    // Potentiometer (ADC)
#define PIN_BUTTON    2    // Taster
#define PIN_BUZZER    3    // Aktiver Buzzer

#define PIN_TM_CLK    8
#define PIN_TM_DIO    9

// --- Konstanten ---
#define MAX_GRAINS    60
#define GRAVITY_THR   4.5f
#define LONG_PRESS_MS 2000
#define ANIM_DELAY_MS 30

// --- Objekte ---
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, PIN_DIN, PIN_CLK, PIN_CS, NUM_DEVICES);
TM1637Display tm(PIN_TM_CLK, PIN_TM_DIO);
CodeCell myCodeCell;

// --- Variablen ---
bool grid[2][8][8]; // Pixel-Speicher
enum State { SETTING, RUNNING, FINISHED };
State state = SETTING;

int topDev = 0, botDev = 1;
int setMinutes = 5;
long remainSec = 0;
int grainsMoved = 0;
unsigned long grainInterval = 1000;
unsigned long grainNextMs = 0, displayNextMs = 0, btnPressStart = 0;
bool btnWasDown = false;

// --- Hilfsfunktionen für die Matrix ---
void setPixel(int dev, int col, int row, bool on) {
  grid[dev][col][row] = on;
  mx.setPoint(row, dev * 8 + col, on);
}

bool getPixel(int dev, int col, int row) {
  return grid[dev][col][row];
}

void clearGrids() {
  mx.clear();
  memset(grid, 0, sizeof(grid));
}

void fillGrid(int dev, int n) {
  int count = 0;
  for (int r = 0; r < 8 && count < n; r++) {
    for (int c = 0; c < 8 && count < n; c++) {
      setPixel(dev, c, r, true);
      count++;
    }
  }
}

// --- Sand-Physik ---
void updateParticles(int dev) {
  for (int r = 6; r >= 0; r--) { // Von unten nach oben scannen
    for (int c = 0; c < 8; c++) {
      if (!getPixel(dev, c, r)) continue;
      
      bool canDown = !getPixel(dev, c, r + 1);
      bool canLeft = (c > 0) && !getPixel(dev, c - 1, r + 1);
      bool canRight = (c < 7) && !getPixel(dev, c + 1, r + 1);

      if (canDown) {
        setPixel(dev, c, r, false);
        setPixel(dev, c, r + 1, true);
      } else if (canLeft && !canRight) {
        setPixel(dev, c, r, false);
        setPixel(dev, c - 1, r + 1, true);
      } else if (canRight && !canLeft) {
        setPixel(dev, c, r, false);
        setPixel(dev, c + 1, r + 1, true);
      } else if (canLeft && canRight) {
        int side = (millis() % 2 == 0) ? 1 : -1;
        setPixel(dev, c, r, false);
        setPixel(dev, c + side, r + 1, true);
      }
    }
  }
}

bool transferGrain() {
  for (int r = 7; r >= 0; r--) {
    for (int c = 7; c >= 0; c--) {
      if (getPixel(topDev, c, r)) {
        setPixel(topDev, c, r, false);
        // In der Mitte der unteren Matrix einfügen
        int startCols[] = {3, 4, 2, 5, 1, 6, 0, 7};
        for (int i = 0; i < 8; i++) {
          if (!getPixel(botDev, startCols[i], 0)) {
            setPixel(botDev, startCols[i], 0, true);
            return true;
          }
        }
        return true; 
      }
    }
  }
  return false;
}

// --- Setup & Loop ---
void setup() {
  Serial.begin(115200);
  myCodeCell.Init(MOTION_ACCELEROMETER);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, 2);
  tm.setBrightness(5);
  clearGrids();
}

void loop() {
  myCodeCell.Run(10);
  bool btnDown = (digitalRead(PIN_BUTTON) == LOW);

  // Taster-Logik
  if (btnDown && !btnWasDown) { btnWasDown = true; btnPressStart = millis(); }
  if (!btnDown && btnWasDown) {
    btnWasDown = false;
    unsigned long duration = millis() - btnPressStart;
    if (state == SETTING) {
      remainSec = (long)setMinutes * 60;
      grainInterval = (remainSec * 1000) / MAX_GRAINS;
      clearGrids();
      fillGrid(topDev, MAX_GRAINS);
      state = RUNNING;
    } else if (state == RUNNING && duration > LONG_PRESS_MS) {
      state = SETTING;
    } else if (state == FINISHED) {
      state = SETTING;
    }
  }

  if (state == SETTING) {
    setMinutes = map(analogRead(PIN_POT), 0, 4095, 1, 99);
    tm.showNumberDecEx(setMinutes * 100, 0b01000000, true);
    delay(50);
  } 
  else if (state == RUNNING) {
    // Schwerkraft checken
    float ax, ay, az;
    myCodeCell.Motion_AccelerometerRead(ax, ay, az);
    if (ay > GRAVITY_THR) { topDev = 0; botDev = 1; }
    else if (ay < -GRAVITY_THR) { topDev = 1; botDev = 0; }

    updateParticles(topDev);
    updateParticles(botDev);

    if (millis() >= grainNextMs && grainsMoved < MAX_GRAINS) {
      if (transferGrain()) {
        grainsMoved++;
        grainNextMs = millis() + grainInterval;
      }
    }

    if (millis() >= displayNextMs) {
      displayNextMs = millis() + 1000;
      if (remainSec > 0) remainSec--;
      int m = remainSec / 60;
      int s = remainSec % 60;
      tm.showNumberDecEx(m * 100 + s, 0b01000000, true);
      if (remainSec <= 0) state = FINISHED;
    }
    delay(ANIM_DELAY_MS);
  }
  else if (state == FINISHED) {
    tm.showNumberDecEx(0, 0b01000000, true);
    digitalWrite(PIN_BUZZER, (millis() % 500 < 250)); // Blink-Piepen
  }
}