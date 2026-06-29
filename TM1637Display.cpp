/**
 * TM1637Display.cpp — TM1637 LED display driver — implementation
 *
 * Part of GPSDO FreeRTOS v0.51
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * Bit-banged I2C-like protocol for TM1637 LED controller.
 * Supports 4-digit (GPSDO_TM1637) and 6-digit (GPSDO_TM1637_6) modes.
 * Adapted for STM32duino from the Arduino TM1637Display library.
 */

//  Author: avishorp@gmail.com
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2.1 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

extern "C" {
  #include <stdlib.h>
  #include <string.h>
  #include <inttypes.h>
}

#include <TM1637Display.h>
#include <Arduino.h>

// Komendy protokołu TM1637
#define TM1637_I2C_COMM1  0x40   // Tryb zapisu, auto-increment adresu
#define TM1637_I2C_COMM2  0xC0   // Adres bazowy rejestrów wyświetlacza (0xC0..0xC5)
#define TM1637_I2C_COMM3  0x80   // Ustawienie jasności i stanu wyświetlacza

//
//      A
//     ---
//  F |   | B
//     -G-
//  E |   | C
//     ---
//      D

// Kody segmentów dla cyfr 0–9 i liter A–F
const uint8_t digitToSegment[] = {
 // XGFEDCBA
  0b00111111,  // 0
  0b00000110,  // 1
  0b01011011,  // 2
  0b01001111,  // 3
  0b01100110,  // 4
  0b01101101,  // 5
  0b01111101,  // 6
  0b00000111,  // 7
  0b01111111,  // 8
  0b01101111,  // 9
  0b01110111,  // A
  0b01111100,  // b
  0b00111001,  // C
  0b01011110,  // d
  0b01111001,  // E
  0b01110001   // F
};

static const uint8_t minusSegments = 0b01000000;  // Znak minus (segment G)


// =============================================================================
// Konstruktor
// =============================================================================
TM1637Display::TM1637Display(uint8_t pinClk, uint8_t pinDIO,
                             uint8_t digitCount,
                             unsigned int bitDelay)
{
  m_pinClk    = pinClk;
  m_pinDIO    = pinDIO;
  m_bitDelay  = bitDelay;
  m_digitCount = (digitCount == 6) ? 6 : 4;   // Obsługujemy tylko 4 lub 6 cyfr

  // Ustaw piny jako wejścia open-drain z pull-up wewnętrznym
  pinMode(m_pinClk, INPUT);
  pinMode(m_pinDIO, INPUT);
  digitalWrite(m_pinClk, LOW);
  digitalWrite(m_pinDIO, LOW);

  // Zastosuj domyślne mapowanie dla danej liczby cyfr
  applyDefaultMapping();
}


// =============================================================================
// applyDefaultMapping
//
// Domyślne mapowania odzwierciedlają rzeczywiste okablowanie popularnych
// modułów TM1637:
//
//   4 cyfry: rejestry TM1637  0  1  2  3
//            cyfry wizualne   0  1  2  3   → m_digitMap = {0,1,2,3}
//
//   6 cyfry: rejestry TM1637  0  1  2  3  4  5
//            cyfry wizualne   2  1  0  5  4  3   → m_digitMap = {2,1,0,5,4,3}
//
//   Czyli m_digitMap[wizualna] = rejestr:
//     4 cyfry: wizualna 0→rejestr 3, 1→2, 2→1, 3→0
//     6 cyfry: wizualna 0→rejestr 2, 1→1, 2→0, 3→5, 4→4, 5→3
// =============================================================================
void TM1637Display::applyDefaultMapping()
{
  if (m_digitCount == 4) {
    // Mapowanie dla 4 cyfr: 0,1,2,3
    setDigitOrder(0, 1, 2, 3);
  } else if (m_digitCount == 6) {
    // Mapowanie dla 6 cyfr: 2,1,0,5,4,3
    // Dwie grupy po trzy cyfry, każda wyświetlana od prawej do lewej
    setDigitOrder(2, 1, 0, 5, 4, 3);
  }
}


// =============================================================================
// setDigitOrder
//
// Ustawia mapowanie: wizualna pozycja N → fizyczny rejestr TM1637 dN.
// Parametry d0..d3 są wymagane. d4, d5 używane tylko dla 6 cyfr.
// =============================================================================
void TM1637Display::setDigitOrder(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                                  uint8_t d4, uint8_t d5)
{
  m_digitMap[0] = d0;
  m_digitMap[1] = d1;
  m_digitMap[2] = d2;
  m_digitMap[3] = d3;
  m_digitMap[4] = d4;   // Ignorowane dla 4-cyfrowego, ale zapisane
  m_digitMap[5] = d5;
}


// =============================================================================
// setBrightness
// =============================================================================
void TM1637Display::setBrightness(uint8_t brightness, bool on)
{
  m_brightness = (brightness & 0x7) | (on ? 0x08 : 0x00);
}


// =============================================================================
// setSegments
//
// Przyjmuje tablicę segmentów indeksowaną WIZUALNIE (segments[0] = lewa cyfra).
// Funkcja przelicza każdą wizualną pozycję na fizyczny rejestr TM1637
// przez m_digitMap, a następnie wysyła dane do układu.
//
// TM1637 wymaga zapisu ciągłego pod kolejne adresy (auto-increment).
// Ponieważ mapowanie jest nieciągłe (np. 2,1,0,5,4,3), musimy najpierw
// zbudować bufor rejestrów, a potem wysłać go jako jeden ciągły blok
// od rejestru 0 do (m_digitCount-1).
// =============================================================================
void TM1637Display::setSegments(const uint8_t segments[], uint8_t length, uint8_t pos)
{
  // Domyślna długość = cały wyświetlacz
  if (length == 0) length = m_digitCount;

  // Zabezpieczenie przed wyjściem poza zakres
  if (pos >= m_digitCount) return;
  if (pos + length > m_digitCount) length = m_digitCount - pos;

  // Bufor rejestrów: registerBuf[numer_rejestru] = bajt_segmentów
  // Inicjalizacja zerem – nieużywane rejestry pozostają wygaszone
  uint8_t registerBuf[TM1637_MAX_DIGITS] = {0, 0, 0, 0, 0, 0};

  // Przepisz segmenty z pozycji wizualnych do odpowiednich rejestrów
  for (uint8_t i = 0; i < length; i++) {
    uint8_t visualPos = pos + i;          // Wizualna pozycja cyfry
    uint8_t regIdx    = m_digitMap[visualPos]; // Fizyczny rejestr TM1637
    registerBuf[regIdx] = segments[i];
  }

  // --- Wysyłanie do TM1637 ---

  // COMM1: tryb zapisu z auto-inkrementacją adresu
  start();
  writeByte(TM1637_I2C_COMM1);
  stop();

  // COMM2: zapis danych od rejestru 0 do (m_digitCount-1)
  // Wysyłamy zawsze pełen blok rejestrów – to jedyny sposób na
  // poprawne działanie przy nieciągłym mapowaniu, bo TM1637
  // nie obsługuje wybiórczego zapisu w trybie auto-increment
  // (chyba że używamy trybu adresowania stałego – patrz niżej).
  start();
  writeByte(TM1637_I2C_COMM2 + 0);       // Adres startowy: rejestr 0
  for (uint8_t r = 0; r < m_digitCount; r++) {
    writeByte(registerBuf[r]);
  }
  stop();

  // COMM3: ustaw jasność
  start();
  writeByte(TM1637_I2C_COMM3 + (m_brightness & 0x0f));
  stop();
}


// =============================================================================
// clear
// =============================================================================
void TM1637Display::clear()
{
  uint8_t data[] = { 0, 0, 0, 0, 0, 0 };
  setSegments(data, m_digitCount, 0);
}


// =============================================================================
// showNumberDec
// =============================================================================
void TM1637Display::showNumberDec(long num, bool leading_zero,
                                  uint8_t length, uint8_t pos)
{
  showNumberDecEx(num, 0, leading_zero, length, pos);
}


// =============================================================================
// showNumberDecEx
// =============================================================================
void TM1637Display::showNumberDecEx(long num, uint8_t dots, bool leading_zero,
                                    uint8_t length, uint8_t pos)
{
  showNumberBaseEx(num < 0 ? -10 : 10,
                   num < 0 ? (uint32_t)(-num) : (uint32_t)num,
                   dots, leading_zero, length, pos);
}


// =============================================================================
// showNumberHexEx
// =============================================================================
void TM1637Display::showNumberHexEx(uint32_t num, uint8_t dots, bool leading_zero,
                                    uint8_t length, uint8_t pos)
{
  showNumberBaseEx(16, num, dots, leading_zero, length, pos);
}


// =============================================================================
// showNumberBaseEx
//
// Wspólna implementacja dla dziesiętnej i szesnastkowej.
// base > 0 → liczba nieujemna w danej podstawie
// base < 0 → liczba ujemna (ze znakiem minus), |base| = podstawa
// =============================================================================
void TM1637Display::showNumberBaseEx(int8_t base, uint32_t num, uint8_t dots,
                                     bool leading_zero,
                                     uint8_t length, uint8_t pos)
{
  bool negative = false;
  if (base < 0) {
    base = -base;
    negative = true;
  }

  // Domyślna długość = cały wyświetlacz
  if (length == 0) length = m_digitCount;

  uint8_t digits[TM1637_MAX_DIGITS];

  if (num == 0 && !leading_zero) {
    // Przypadek szczególny: sama cyfra 0 bez zer wiodących
    for (uint8_t i = 0; i < length - 1; i++)
      digits[i] = 0;                     // Puste miejsca
    digits[length - 1] = encodeDigit(0); // Samo zero na końcu
  }
  else {
    for (int i = length - 1; i >= 0; i--) {
      uint8_t digit = (uint8_t)(num % base);

      if (digit == 0 && num == 0 && !leading_zero)
        digits[i] = 0;                   // Zero wiodące → puste miejsce
      else
        digits[i] = encodeDigit(digit);

      // Umieść znak minus przed pierwszą cyfrą niezerową
      if (num == 0 && negative) {
        digits[i] = minusSegments;
        negative = false;
      }

      num /= base;
    }
  }

  // Nałóż kropki dziesiętne / dwukropek
  if (dots != 0)
    showDots(dots, digits, length);

  setSegments(digits, length, pos);
}


// =============================================================================
// showDots
//
// Ustawia bit SEG_DP (0x80) w odpowiednich cyfrach zgodnie z maską dots.
// Bit 7 (MSB) maski → kropka po wizualnej pozycji 0
// Bit 6         → kropka po wizualnej pozycji 1
// itd.
// =============================================================================
void TM1637Display::showDots(uint8_t dots, uint8_t* digits, uint8_t length)
{
  for (int i = 0; i < length; i++) {
    digits[i] |= (dots & 0x80);
    dots <<= 1;
  }
}


// =============================================================================
// encodeDigit
// =============================================================================
uint8_t TM1637Display::encodeDigit(uint8_t digit)
{
  return digitToSegment[digit & 0x0f];
}


// =============================================================================
// Metody komunikacji (protokół TM1637 – niezmienione względem oryginału)
// =============================================================================

void TM1637Display::bitDelay()
{
  delayMicroseconds(m_bitDelay);
}

void TM1637Display::start()
{
  pinMode(m_pinDIO, OUTPUT);
  bitDelay();
}

void TM1637Display::stop()
{
  pinMode(m_pinDIO, OUTPUT);
  bitDelay();
  pinMode(m_pinClk, INPUT);
  bitDelay();
  pinMode(m_pinDIO, INPUT);
  bitDelay();
}

bool TM1637Display::writeByte(uint8_t b)
{
  uint8_t data = b;

  // Wyślij 8 bitów (LSB first)
  for (uint8_t i = 0; i < 8; i++) {
    // CLK → LOW
    pinMode(m_pinClk, OUTPUT);
    bitDelay();

    // Ustaw bit danych
    if (data & 0x01)
      pinMode(m_pinDIO, INPUT);    // HIGH (open-drain z pull-up)
    else
      pinMode(m_pinDIO, OUTPUT);   // LOW
    bitDelay();

    // CLK → HIGH
    pinMode(m_pinClk, INPUT);
    bitDelay();

    data >>= 1;
  }

  // Oczekiwanie na ACK od TM1637
  pinMode(m_pinClk, OUTPUT);
  pinMode(m_pinDIO, INPUT);
  bitDelay();

  pinMode(m_pinClk, INPUT);
  bitDelay();

  uint8_t ack = digitalRead(m_pinDIO);
  if (ack == 0)
    pinMode(m_pinDIO, OUTPUT);

  bitDelay();
  pinMode(m_pinClk, OUTPUT);
  bitDelay();

  return ack;
}
