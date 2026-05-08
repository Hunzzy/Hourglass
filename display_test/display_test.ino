/*
 * ============================================================
 *  LED Display Test — ESP32-C3 Mini (CodeCell)
 * ============================================================
 *
 *  Testet:
 *    1) TM1637  → 4-Digit 7-Segment Display
 *    2) Matrix A (Adresse 0) → 8×8 LED-Matrix
 *    3) Matrix B (Adresse 1) → 8×8 LED-Matrix
 *
 *  Verdrahtung (gleich wie Sanduhr-Projekt):
 *    MAX7219: G7=DIN, G5=CLK, G6=CS, 4.4V=VCC, GND=GND
 *    TM1637:  G8=CLK, G9=DIO, 3.3V=VCC, GND=GND
 *
 *  Testablauf (Serial Monitor öffnen: 115200 Baud):
 *    Phase 1 — TM1637:   Zählt 00:00 → 00:05, zeigt "8888", dann "--:--"
 *    Phase 2 — Matrix A: Alle LEDs AN, dann AUS, dann Schachbrett
 *    Phase 3 — Matrix B: Alle LEDs AN, dann AUS, dann Schachbrett
 *    Phase 4 — Beide:    Läuft ein Pixel Zeile für Zeile durch beide Matrizen
 *    Danach: Endlosschleife mit Helligkeitstest
 * ============================================================
 */

#include "LedControl.h"
#include <TM1637Display.h>

// ── Pins ──────────────────────────────────────────────────────────────────────
#define PIN_DIN    7
#define PIN_CLK    5
#define PIN_CS     6
#define PIN_TM_CLK 8
#define PIN_TM_DIO 9

// ── Objekte ───────────────────────────────────────────────────────────────────
LedControl    lc(PIN_DIN, PIN_CLK, PIN_CS, 2);
TM1637Display tm(PIN_TM_CLK, PIN_TM_DIO);

// ── Hilfsfunktionen ───────────────────────────────────────────────────────────

void matrixAllOn(int addr) {
  for (int row = 0; row < 8; row++)
    lc.setRow(addr, row, 0xFF);
}

void matrixAllOff(int addr) {
  lc.clearDisplay(addr);
}

void matrixCheckerboard(int addr, bool invert) {
  for (int row = 0; row < 8; row++) {
    byte val = ((row % 2) == 0)
               ? (invert ? 0b01010101 : 0b10101010)
               : (invert ? 0b10101010 : 0b01010101);
    lc.setRow(addr, row, val);
  }
}

void matrixSmiley(int addr) {
  // Smiley-Gesicht
  lc.setRow(addr, 0, 0b00111100);
  lc.setRow(addr, 1, 0b01000010);
  lc.setRow(addr, 2, 0b10100101);
  lc.setRow(addr, 3, 0b10000001);
  lc.setRow(addr, 4, 0b10100101);
  lc.setRow(addr, 5, 0b10011001);
  lc.setRow(addr, 6, 0b01000010);
  lc.setRow(addr, 7, 0b00111100);
}

void matrixArrow(int addr, bool up) {
  if (up) {
    lc.setRow(addr, 0, 0b00011000);
    lc.setRow(addr, 1, 0b00111100);
    lc.setRow(addr, 2, 0b01111110);
    lc.setRow(addr, 3, 0b11111111);
    lc.setRow(addr, 4, 0b00011000);
    lc.setRow(addr, 5, 0b00011000);
    lc.setRow(addr, 6, 0b00011000);
    lc.setRow(addr, 7, 0b00011000);
  } else {
    lc.setRow(addr, 0, 0b00011000);
    lc.setRow(addr, 1, 0b00011000);
    lc.setRow(addr, 2, 0b00011000);
    lc.setRow(addr, 3, 0b00011000);
    lc.setRow(addr, 4, 0b11111111);
    lc.setRow(addr, 5, 0b01111110);
    lc.setRow(addr, 6, 0b00111100);
    lc.setRow(addr, 7, 0b00011000);
  }
}

void pause(int ms, const char* msg) {
  Serial.println(msg);
  delay(ms);
}

// ═════════════════════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("========================================");
  Serial.println("  LED Display Test — Sanduhr Projekt");
  Serial.println("========================================");

  // TM1637 initialisieren
  tm.setBrightness(7);
  tm.showNumberDecEx(0, 0b01000000, true);  // "00:00"

  // MAX7219 initialisieren
  for (int i = 0; i < 2; i++) {
    lc.shutdown(i, false);
    lc.setIntensity(i, 8);
    lc.clearDisplay(i);
  }

  delay(500);

  // ══════════════════════════════════════════════════
  //  PHASE 1: TM1637 Test
  // ══════════════════════════════════════════════════
  Serial.println("\n--- PHASE 1: TM1637 (4-Digit Display) ---");

  // Zählt 00:00 bis 00:05
  Serial.println("Zähle 00:00 → 00:05 ...");
  for (int s = 0; s <= 5; s++) {
    tm.showNumberDecEx(s, 0b01000000, true);
    Serial.print("  Anzeige: 00:0"); Serial.println(s);
    delay(600);
  }

  // Alle Segmente an ("8888")
  pause(100, "Alle Segmente AN (8888) ...");
  tm.showNumberDec(8888, true);
  delay(1500);

  // Doppelpunkt blinken
  Serial.println("Doppelpunkt blinkt 4× ...");
  for (int i = 0; i < 4; i++) {
    tm.showNumberDecEx(1234, 0b01000000, false);  // mit Doppelpunkt
    delay(400);
    tm.showNumberDecEx(1234, 0b00000000, false);  // ohne Doppelpunkt
    delay(400);
  }

  // Display leer
  pause(100, "Display leer ...");
  uint8_t dashes[] = {
    SEG_G, SEG_G, SEG_G, SEG_G  // "----"
  };
  tm.setSegments(dashes);
  delay(1000);

  tm.showNumberDecEx(0, 0b01000000, true);  // zurück auf "00:00"
  Serial.println("→ TM1637 OK");

  // ══════════════════════════════════════════════════
  //  PHASE 2: Matrix A (Adresse 0)
  // ══════════════════════════════════════════════════
  Serial.println("\n--- PHASE 2: LED-Matrix A (Adresse 0) ---");

  pause(300, "Alle LEDs AN ...");
  matrixAllOn(0);
  delay(1500);

  pause(300, "Alle LEDs AUS ...");
  matrixAllOff(0);
  delay(800);

  pause(300, "Schachbrett Muster A ...");
  matrixCheckerboard(0, false);
  delay(1000);

  pause(300, "Schachbrett Muster B ...");
  matrixCheckerboard(0, true);
  delay(1000);

  pause(300, "Smiley ...");
  matrixSmiley(0);
  delay(1500);

  pause(300, "Pfeil HOCH ...");
  matrixArrow(0, true);
  delay(1200);

  matrixAllOff(0);
  Serial.println("→ Matrix A OK");

  // ══════════════════════════════════════════════════
  //  PHASE 3: Matrix B (Adresse 1)
  // ══════════════════════════════════════════════════
  Serial.println("\n--- PHASE 3: LED-Matrix B (Adresse 1) ---");

  pause(300, "Alle LEDs AN ...");
  matrixAllOn(1);
  delay(1500);

  pause(300, "Alle LEDs AUS ...");
  matrixAllOff(1);
  delay(800);

  pause(300, "Schachbrett Muster A ...");
  matrixCheckerboard(1, false);
  delay(1000);

  pause(300, "Schachbrett Muster B ...");
  matrixCheckerboard(1, true);
  delay(1000);

  pause(300, "Smiley ...");
  matrixSmiley(1);
  delay(1500);

  pause(300, "Pfeil RUNTER ...");
  matrixArrow(1, false);
  delay(1200);

  matrixAllOff(1);
  Serial.println("→ Matrix B OK");

  // ══════════════════════════════════════════════════
  //  PHASE 4: Laufpixel durch beide Matrizen
  // ══════════════════════════════════════════════════
  Serial.println("\n--- PHASE 4: Laufpixel durch Matrix A → B ---");
  lc.clearDisplay(0);
  lc.clearDisplay(1);

  for (int addr = 0; addr < 2; addr++) {
    Serial.print("  Matrix "); Serial.println(addr);
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        lc.clearDisplay(addr);
        lc.setRawXY(addr, col, row, true);
        delay(40);
      }
    }
    lc.clearDisplay(addr);
  }
  Serial.println("→ Laufpixel OK");

  // ══════════════════════════════════════════════════
  //  PHASE 5: Helligkeitstest
  // ══════════════════════════════════════════════════
  Serial.println("\n--- PHASE 5: Helligkeitstest (0 → 15 → 0) ---");
  matrixAllOn(0);
  matrixAllOn(1);
  for (int b = 0; b <= 15; b++) {
    lc.setIntensity(0, b);
    lc.setIntensity(1, b);
    tm.setBrightness(b > 7 ? 7 : b);
    Serial.print("  Helligkeit: "); Serial.println(b);
    delay(120);
  }
  for (int b = 15; b >= 0; b--) {
    lc.setIntensity(0, b);
    lc.setIntensity(1, b);
    tm.setBrightness(b > 7 ? 7 : b);
    delay(120);
  }
  lc.setIntensity(0, 5);
  lc.setIntensity(1, 5);
  tm.setBrightness(5);

  Serial.println("\n========================================");
  Serial.println("  Alle Tests abgeschlossen!");
  Serial.println("  Beide Matrizen zeigen jetzt abwechselnd");
  Serial.println("  Smiley / Pfeile in Endlosschleife.");
  Serial.println("========================================\n");
}

// ═════════════════════════════════════════════════════════════════════════════
//  LOOP — Endlosschleife: Animation auf beiden Matrizen
// ═════════════════════════════════════════════════════════════════════════════

void loop() {
  // Matrix A: Pfeil hoch, Matrix B: Pfeil runter (Sanduhr-Symbol)
  matrixArrow(0, true);
  matrixArrow(1, false);
  tm.showNumberDecEx(1234, 0b01000000, false);
  delay(1200);

  // Beide: Schachbrett A
  matrixCheckerboard(0, false);
  matrixCheckerboard(1, true);
  delay(800);

  // Beide: Schachbrett B
  matrixCheckerboard(0, true);
  matrixCheckerboard(1, false);
  delay(800);

  // Beide: Smiley
  matrixSmiley(0);
  matrixSmiley(1);
  tm.showNumberDecEx(8888, 0b01000000, false);
  delay(1200);

  // Beide: Alle AUS
  lc.clearDisplay(0);
  lc.clearDisplay(1);
  tm.showNumberDecEx(0, 0b01000000, true);
  delay(500);
}
