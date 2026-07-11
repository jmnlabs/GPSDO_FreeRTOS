/**
 * TM1637Display.h — TM1637 LED display driver — header
 *
 * Part of GPSDO FreeRTOS v0.91
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * Based on: GPSDO v0.06c by André Balsa
 * AI:       Claude (Anthropic)
 *
 *
 * Bit-banged driver for 4-digit and 6-digit TM1637 clock displays.
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
//
//  Rozszerzenie: obsługa 4 i 6 cyfr z poprawnym mapowaniem fizycznych
//  rejestrów TM1637 do wizualnych pozycji cyfr.
//
//  MAPOWANIA DOMYŚLNE:
//    4 cyfry: rejestr→cyfra  [3,2,1,0]      (odwrotna kolejność)
//    6 cyfr:  rejestr→cyfra  [2,1,0,5,4,3]  (dwie grupy po 3, każda odwrócona)
//
//  Jeśli Twój moduł wyświetla cyfry w złej kolejności, użyj setDigitOrder()
//  lub konstruktora z parametrem digitCount=6 dla modułu 6-cyfrowego.

#ifndef __TM1637DISPLAY__
#define __TM1637DISPLAY__

#include <inttypes.h>

// Definicje segmentów
#define SEG_A   0b00000001
#define SEG_B   0b00000010
#define SEG_C   0b00000100
#define SEG_D   0b00001000
#define SEG_E   0b00010000
#define SEG_F   0b00100000
#define SEG_G   0b01000000
#define SEG_DP  0b10000000

#define DEFAULT_BIT_DELAY  100

// Maksymalna obsługiwana liczba cyfr
#define TM1637_MAX_DIGITS  6


class TM1637Display {

public:
  //! @brief Konstruktor – inicjalizuje wyświetlacz 4 lub 6 cyfrowy.
  //!
  //! Domyślne mapowanie rejestrów jest ustawiane automatycznie:
  //!   4 cyfry → kolejność 3,2,1,0
  //!   6 cyfry → kolejność 2,1,0,5,4,3
  //!
  //! Jeśli Twój moduł wyświetla cyfry w innej kolejności, wywołaj
  //! setDigitOrder() po inicjalizacji.
  //!
  //! @param pinClk     Pin zegarowy (CLK)
  //! @param pinDIO     Pin danych (DIO)
  //! @param digitCount Liczba cyfr: 4 (domyślnie) lub 6
  //! @param bitDelay   Opóźnienie między bitami w µs (domyślnie 100)
  TM1637Display(uint8_t pinClk, uint8_t pinDIO,
                uint8_t digitCount = 4,
                unsigned int bitDelay = DEFAULT_BIT_DELAY);

  //! @brief Ustawia jasność wyświetlacza.
  //!
  //! @param brightness  Wartość 0 (najciemniej) do 7 (najjaśniej)
  //! @param on          true = wyświetlacz włączony, false = wyłączony
  void setBrightness(uint8_t brightness, bool on = true);

  //! @brief Ręczne ustawienie mapowania wizualna-pozycja → rejestr TM1637.
  //!
  //! Parametry to fizyczne numery rejestrów TM1637 (0..5) przypisane
  //! kolejno do wizualnych pozycji 0,1,2,3,[4,5].
  //!
  //! Przykłady:
  //!   4 cyfry normalne:    setDigitOrder(0, 1, 2, 3)
  //!   4 cyfry odwrócone:   setDigitOrder(3, 2, 1, 0)   ← domyślne
  //!   6 cyfr diymore:      setDigitOrder(2, 1, 0, 5, 4, 3)  ← domyślne
  //!
  //! @param d0..d5  Numery rejestrów dla wizualnych pozycji 0..5
  //!               (d4, d5 ignorowane dla wyświetlacza 4-cyfrowego)
  void setDigitOrder(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3,
                     uint8_t d4 = 4, uint8_t d5 = 5);

  //! @brief Wyświetla surowe wartości segmentów.
  //!
  //! Tablica segments[] jest indeksowana WIZUALNIE:
  //!   segments[0] = lewa cyfra, segments[N-1] = prawa cyfra.
  //! Biblioteka automatycznie przelicza kolejność wysyłania do TM1637
  //! zgodnie z bieżącym mapowaniem.
  //!
  //! @param segments  Wskaźnik na tablicę bajtów segmentów
  //! @param length    Liczba cyfr do ustawienia (domyślnie = digitCount)
  //! @param pos       Wizualna pozycja startowa (0 = najlewa)
  void setSegments(const uint8_t segments[], uint8_t length = 0, uint8_t pos = 0);

  //! @brief Czyści wyświetlacz (wszystkie cyfry wygaszone).
  void clear();

  //! @brief Wyświetla liczbę dziesiętną.
  //!
  //! Zakres:
  //!   4 cyfry: -999 do 9999
  //!   6 cyfry: -99999 do 999999
  //!
  //! @param num          Liczba do wyświetlenia
  //! @param leading_zero Czy wyświetlać zera wiodące
  //! @param length       Liczba cyfr do użycia (0 = cały wyświetlacz)
  //! @param pos          Wizualna pozycja najbardziej znaczącej cyfry
  void showNumberDec(long num, bool leading_zero = false,
                     uint8_t length = 0, uint8_t pos = 0);

  //! @brief Wyświetla liczbę dziesiętną z kontrolą kropek dziesiętnych / dwukropka.
  //!
  //! Maska dots — bity od MSB odpowiadają pozycjom od lewej:
  //!   bit7 = kropka/dwukropek po pozycji 0
  //!   bit6 = kropka/dwukropek po pozycji 1
  //!   bit5 = kropka/dwukropek po pozycji 2
  //!   bit4 = kropka/dwukropek po pozycji 3
  //!   bit3 = kropka/dwukropek po pozycji 4  (tylko 6 cyfr)
  //!
  //! Przykłady dla 6 cyfr:
  //!   HH:MM:SS → dots = 0b00100100 (po pozycji 1 i 3)
  //!   000.000  → dots = 0b00010000 (po pozycji 2)
  //!
  //! Przykłady dla 4 cyfr:
  //!   HH:MM    → dots = 0b01000000 (po pozycji 1)
  //!
  //! @param num          Liczba do wyświetlenia
  //! @param dots         Maska włączenia kropek / dwukropka
  //! @param leading_zero Czy wyświetlać zera wiodące
  //! @param length       Liczba cyfr do użycia (0 = cały wyświetlacz)
  //! @param pos          Wizualna pozycja najbardziej znaczącej cyfry
  void showNumberDecEx(long num, uint8_t dots = 0, bool leading_zero = false,
                       uint8_t length = 0, uint8_t pos = 0);

  //! @brief Wyświetla liczbę szesnastkową z kontrolą kropek.
  //!
  //! Zakres: 0x0000..0xFFFF (4 cyfry) lub 0x000000..0xFFFFFF (6 cyfr).
  //!
  //! @param num          Liczba do wyświetlenia
  //! @param dots         Maska włączenia kropek
  //! @param leading_zero Czy wyświetlać zera wiodące
  //! @param length       Liczba cyfr do użycia (0 = cały wyświetlacz)
  //! @param pos          Wizualna pozycja najbardziej znaczącej cyfry
  void showNumberHexEx(uint32_t num, uint8_t dots = 0, bool leading_zero = false,
                       uint8_t length = 0, uint8_t pos = 0);

  //! @brief Konwertuje cyfrę (0–15) na bajt segmentów 7-segmentowych.
  //! @param digit  Cyfra 0–9 lub litera A–F (10–15)
  //! @return Bajt z zakodowanymi segmentami
  static uint8_t encodeDigit(uint8_t digit);

protected:
  void     bitDelay();
  void     start();
  void     stop();
  bool     writeByte(uint8_t b);
  void     showDots(uint8_t dots, uint8_t* digits, uint8_t length);
  void     showNumberBaseEx(int8_t base, uint32_t num, uint8_t dots,
                            bool leading_zero, uint8_t length, uint8_t pos);
  void     applyDefaultMapping();

private:
  uint8_t      m_pinClk;
  uint8_t      m_pinDIO;
  uint8_t      m_brightness;
  unsigned int m_bitDelay;
  uint8_t      m_digitCount;                  // Liczba cyfr: 4 lub 6

  // Mapa: m_digitMap[wizualna_pozycja] = numer_rejestru_TM1637
  // Przykład dla 6 cyfr (diymore): {2,1,0,5,4,3}
  uint8_t      m_digitMap[TM1637_MAX_DIGITS];
};

#endif // __TM1637DISPLAY__
