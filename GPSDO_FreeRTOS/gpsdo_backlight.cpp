/**
 * gpsdo_backlight.cpp — TFT backlight dimming on PB5 (TIM3 CH2)
 *
 * Part of GPSDO v1.07.57rt
 * Author:   J. M. Niewiński
 * GitHub:   https://github.com/jmnlabs/GPSDO_FreeRTOS
 * AI:       Claude Opus 5 (Anthropic), GLM-5.3 Max (Z.ai), Qwen3.8-Max
 *
 * See gpsdo_backlight.h for the MOSFET stage this drives and why the duty is
 * inverted. This file is the timer.
 */

#include <Arduino.h>
#include "gpsdo_config.h"
#include "gpsdo_backlight.h"

#if defined(GPSDO_TFT_BL_PWM)
#include "stm32f4xx_hal.h"

/* 100 MHz / 5000 = 20.0 kHz.
 *
 * Chosen high rather than convenient. Twenty kilohertz is above the audio
 * band, so nothing in the enclosure sings; it is far above anything the loop
 * or the ADC sampling can alias with; and 5000 counts is 12 bits of dimming
 * range, which is four hundred times finer than the 71 settings the CLI can
 * even ask for. Going higher would start to matter for the MOSFET's switching
 * losses against a 47 ohm gate resistor for no benefit at all. */
#define BL_PERIOD   5000u

uint8_t g_tft_bl_pct = BL_PCT_DEF;

static bool s_bl_ready = false;

static void bl_apply(void)
{
    if (!s_bl_ready) return;
    /* THE COMPLEMENT, because the stage inverts: PWM mode 1 holds the pin high
     * while CNT < CCR, a high gate turns the P-channel OFF, so the high time
     * is the DARK time. 100% therefore lands on CCR = 0 — a pin held
     * statically low, with the stage not switching at all. */
    uint32_t dark = (uint32_t)(BL_PCT_MAX - g_tft_bl_pct);
    TIM3->CCR2 = (dark * BL_PERIOD) / BL_PCT_MAX;
}

void backlight_set(uint8_t pct)
{
    if (pct < BL_PCT_MIN) pct = BL_PCT_MIN;
    if (pct > BL_PCT_MAX) pct = BL_PCT_MAX;
    g_tft_bl_pct = pct;
    bl_apply();
}

void backlight_begin(void)
{
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, LOW);      /* full brightness while the timer starts */

    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* PB5 -> AF2 (TIM3_CH2), push-pull. Speed is deliberately LOW rather than
     * HIGH: this drives a gate through 47 ohm at 20 kHz, and the slowest edge
     * that still switches cleanly is the one that puts least on the 3.3 V rail
     * — which is the same rail VDDA and therefore the ADC reference sits on. */
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_5;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &g);

    TIM3->CR1  = 0;
    TIM3->PSC  = 0;                     /* TIM3 is on APB1: 100 MHz here */
    TIM3->ARR  = BL_PERIOD - 1u;
    TIM3->CCR2 = 0;                     /* full brightness until bl_apply runs */

    /* CH2: PWM mode 1, compare preloaded, so a brightness change lands at the
     * next update instead of truncating the pulse it arrives in. */
    TIM3->CCMR1 &= ~(TIM_CCMR1_OC2M | TIM_CCMR1_CC2S);
    TIM3->CCMR1 |= (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    TIM3->CCER  |= TIM_CCER_CC2E;
    TIM3->CR1   |= TIM_CR1_ARPE;
    TIM3->EGR    = TIM_EGR_UG;
    TIM3->CR1   |= TIM_CR1_CEN;

    s_bl_ready = true;
    bl_apply();                         /* whatever recall or the default left */
}

#else  /* no backlight hardware compiled in */

/* The variable exists either way so the settings block and the CLI do not have
 * to be conditional in three more places; nothing reads it when the feature is
 * off, and the setter is a store with a clamp. */
uint8_t g_tft_bl_pct = BL_PCT_DEF;

void backlight_begin(void) { }

void backlight_set(uint8_t pct)
{
    if (pct < BL_PCT_MIN) pct = BL_PCT_MIN;
    if (pct > BL_PCT_MAX) pct = BL_PCT_MAX;
    g_tft_bl_pct = pct;
}

#endif /* GPSDO_TFT_BL_PWM */
