#include <Arduino.h>

// --- Ayarlar ---
#define FLOW_PIN 25          // Direnç bölücünün ESP32'ye geldiği pin
#define TIMEOUT_US 500000UL  // 500 ms; gerekirse 1_000_000 yap

void setup() {
  Serial.begin(9600);
  delay(200);

  // Direnç bölücü kullanıyorsan INPUT (iç pullup/down kapalı)
  pinMode(FLOW_PIN, INPUT);

  Serial.println("Akis sensoru PULSE TEST (interruptsuz, pulseIn ile)");
  Serial.println("Rapor: tHIGH(us), tLOW(us), freq(Hz), duty(%)");
}

void loop() {
  // Önce HIGH süresi, sonra LOW süresi ölçülür
  // Not: pulseIn, belirtilen seviyeye GELEN kenarı bekler, sonra süresini ölçer.
  unsigned long tHigh = pulseIn(FLOW_PIN, HIGH, TIMEOUT_US);
  unsigned long tLow  = pulseIn(FLOW_PIN, LOW,  TIMEOUT_US);

  if (tHigh == 0 && tLow == 0) {
    // Zaman asimi: veri yakalanamadi
    Serial.println("Pulse yok (timeout). Hat/besleme/bölücü/ground kontrol et.");
  } else {
    // Birini bulduysak digerini de bulmaya calis
    // (bazen ilk okuma 0 olabilir; tekrar denemek istersen burada ikinci ölçüm ekleyebilirsin)

    // Period ve frekans
    double period = (double)tHigh + (double)tLow;   // us
    double freqHz = (period > 0.0) ? (1000000.0 / period) : 0.0;
    double duty   = (period > 0.0) ? (100.0 * (double)tHigh / period) : 0.0;

    Serial.print("tH=");
    Serial.print(tHigh);
    Serial.print("us, tL=");
    Serial.print(tLow);
    Serial.print("us, f=");
    Serial.print(freqHz, 2);
    Serial.print(" Hz, duty=");
    Serial.print(duty, 1);
    Serial.println("%");
  }

  delay(250);  // 4 Hz rapor
}
