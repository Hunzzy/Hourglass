/*
 * ============================================================
 * DIAGNOSE-TOOL: Poti, Taster & Buzzer (mit tone-Befehl)
 * ============================================================
 */

#include <Arduino.h>

// Pin-Definitionen
#define PIN_POT      1    // Potentiometer an G1
#define PIN_BUTTON   2    // Taster an G2
#define PIN_BUZZER   3    // Buzzer (+) an G3, (-) an GND

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("--- DIAGNOSE MIT TONE() START ---");

  // Konfiguration der Pins
  pinMode(PIN_BUTTON, INPUT_PULLUP); 
  pinMode(PIN_BUZZER, OUTPUT);       
  
  // Kurzer Einschalt-Test: 2000 Hz für 200 Millisekunden
  Serial.println("Test: Buzzer piept kurz mit 2000 Hz...");
  tone(PIN_BUZZER, 2000, 200); 
  delay(300); // Kurz warten, damit der Ton ausklingen kann
  
  analogReadResolution(12); // ESP32-C3 Standard: 0-4095
}

void loop() {
  // 1. POTENTIOMETER AUSLESEN
  int potValue = analogRead(PIN_POT);
  
  // 2. TASTER AUSLESEN (LOW = gedrückt)
  bool isPressed = (digitalRead(PIN_BUTTON) == LOW); 

  // 3. AUSGABE IM SERIAL MONITOR
  Serial.print("Poti-Wert: ");
  Serial.print(potValue);
  Serial.print(" | Taster: ");
  Serial.println(isPressed ? "GEDRUECKT" : "OFFEN");

  // 4. BUZZER MIT TONE() STEUERN
  if (isPressed) {
    // Wandelt den Poti-Wert (0-4095) in eine hörbare Frequenz um (100 Hz bis 3000 Hz)
    int pitch = map(potValue, 0, 4095, 100, 3000);
    tone(PIN_BUZZER, pitch); 
  } else {
    // Schaltet den Ton sofort ab, wenn der Taster losgelassen wird
    noTone(PIN_BUZZER);  
  }

  delay(50); // Etwas kürzere Pause für flüssigere Tonänderungen
}