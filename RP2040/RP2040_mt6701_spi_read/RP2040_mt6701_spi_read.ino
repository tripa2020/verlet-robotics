/*
 * MT6701 SSI Raw Diagnostic - RP2040-Zero using SPI0
 *
 * RP2040-Zero SPI0 pins:
 *   SCK:   GPIO2
 *   MISO:  GPIO0
 *   CS:    GPIO1 (manually controlled)
 *
 * MT6701 wiring:
 *   B (CLK) -> GPIO2
 *   A (DO)  -> GPIO0
 *   Z (CSN) -> GPIO1
 *   VDD     -> 3.3V
 *   GND     -> GND
 */

#include <Arduino.h>
#include <SPI.h>

#define CS_PIN   1
#define MISO_PIN 0
#define SCK_PIN  2

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  // Test MISO pin directly before SPI init
  pinMode(MISO_PIN, INPUT);
  Serial.print("MISO (GPIO0) state: ");
  Serial.println(digitalRead(MISO_PIN));

  // Test CLK pin manually - scope or LED can verify signal
  pinMode(SCK_PIN, OUTPUT);
  Serial.print("Toggling CLK (GPIO2) 10 times... ");
  for (int i = 0; i < 10; i++) {
    digitalWrite(SCK_PIN, HIGH);
    delayMicroseconds(100);
    digitalWrite(SCK_PIN, LOW);
    delayMicroseconds(100);
  }
  Serial.println("done");

  // Check if CSN is working
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(10);
  Serial.print("MISO with CS LOW: ");
  Serial.println(digitalRead(MISO_PIN));
  digitalWrite(CS_PIN, HIGH);
  Serial.print("MISO with CS HIGH: ");
  Serial.println(digitalRead(MISO_PIN));

  // Configure SPI pins for RP2040
  SPI.setRX(MISO_PIN);
  SPI.setSCK(SCK_PIN);
  SPI.begin();

  Serial.println("\nMT6701 SSI Raw Diagnostic (RP2040-Zero SPI0)");
  Serial.println("Testing SPI modes...");
  Serial.println();
}

void readMT6701(uint8_t spiMode, const char* modeName) {
  uint8_t data[3];

  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, spiMode));
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(1);

  data[0] = SPI.transfer(0xFF);
  data[1] = SPI.transfer(0xFF);
  data[2] = SPI.transfer(0xFF);

  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();

  // Parse angle (14-bit, bits 23-10 of 24-bit frame)
  uint16_t angle_raw = ((uint16_t)data[0] << 6) | (data[1] >> 2);
  float angle = angle_raw * (360.0f / 16384.0f);

  // Parse status (bits 9-6)
  uint8_t status = ((data[1] & 0x03) << 2) | (data[2] >> 6);
  uint8_t field = status & 0x03;

  Serial.print(modeName);
  Serial.print(" | raw: 0x");
  if (data[0] < 0x10) Serial.print("0");
  Serial.print(data[0], HEX);
  Serial.print(" 0x");
  if (data[1] < 0x10) Serial.print("0");
  Serial.print(data[1], HEX);
  Serial.print(" 0x");
  if (data[2] < 0x10) Serial.print("0");
  Serial.print(data[2], HEX);

  Serial.print(" | angle: ");
  Serial.print(angle, 2);

  Serial.print(" | field: ");
  switch (field) {
    case 0: Serial.print("NORM"); break;
    case 1: Serial.print("STRONG"); break;
    case 2: Serial.print("WEAK"); break;
    default: Serial.print("ERR"); break;
  }
  Serial.println();
}

// Bit-banged SSI read - bypasses hardware SPI
void readMT6701_bitbang() {
  uint32_t data = 0;

  // Use GPIO mode, not SPI hardware
  SPI.end();
  pinMode(SCK_PIN, OUTPUT);   // CLK
  pinMode(MISO_PIN, INPUT);   // MISO

  digitalWrite(SCK_PIN, LOW);
  digitalWrite(CS_PIN, LOW);
  delayMicroseconds(1);

  // Read 24 bits, MSB first
  for (int i = 0; i < 24; i++) {
    digitalWrite(SCK_PIN, HIGH);
    delayMicroseconds(1);
    data <<= 1;
    if (digitalRead(MISO_PIN)) data |= 1;
    digitalWrite(SCK_PIN, LOW);
    delayMicroseconds(1);
  }

  digitalWrite(CS_PIN, HIGH);

  // Re-init SPI for next loop
  SPI.setRX(MISO_PIN);
  SPI.setSCK(SCK_PIN);
  SPI.begin();

  uint8_t b0 = (data >> 16) & 0xFF;
  uint8_t b1 = (data >> 8) & 0xFF;
  uint8_t b2 = data & 0xFF;

  uint16_t angle_raw = ((uint16_t)b0 << 6) | (b1 >> 2);
  float angle = angle_raw * (360.0f / 16384.0f);
  uint8_t field = ((b1 & 0x03) << 2 | (b2 >> 6)) & 0x03;

  Serial.print("BITBANG | raw: 0x");
  if (b0 < 0x10) Serial.print("0");
  Serial.print(b0, HEX);
  Serial.print(" 0x");
  if (b1 < 0x10) Serial.print("0");
  Serial.print(b1, HEX);
  Serial.print(" 0x");
  if (b2 < 0x10) Serial.print("0");
  Serial.print(b2, HEX);
  Serial.print(" | angle: ");
  Serial.print(angle, 2);
  Serial.print(" | field: ");
  switch (field) {
    case 0: Serial.print("NORM"); break;
    case 1: Serial.print("STRONG"); break;
    case 2: Serial.print("WEAK"); break;
    default: Serial.print("ERR"); break;
  }
  Serial.println();
}

void loop() {
  readMT6701_bitbang();
  readMT6701(SPI_MODE0, "MODE0");
  readMT6701(SPI_MODE1, "MODE1");
  Serial.println("---");

  delay(500);
}
