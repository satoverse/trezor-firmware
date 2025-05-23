/*
 * This file is part of the Trezor project, https://trezor.io/
 *
 * Copyright (c) SatoshiLabs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <trezor_bsp.h>
#include <trezor_rtl.h>

#include <sys/systick.h>

#include <io/backlight.h>

// Requested PWM Timer clock frequency [Hz]
#define TIM_FREQ 10000000
// Prescaler divider for the PWM Timer
#define LED_PWM_PRESCALER (SystemCoreClock / TIM_FREQ - 1)
// Period of the PWM Timer
#define LED_PWM_TIM_PERIOD (TIM_FREQ / TPS61043_FREQ)

// Backlight driver state
typedef struct {
  // Set if driver is initialized
  bool initialized;
  // Current backlight level in range 0-255
  int current_level;

} backlight_driver_t;

// Backlight driver instance
static backlight_driver_t g_backlight_driver = {
    .initialized = false,
};

void backlight_init(backlight_action_t action) {
  backlight_driver_t *drv = &g_backlight_driver;

  if (drv->initialized) {
    return;
  }

  memset(drv, 0, sizeof(backlight_driver_t));

  int initial_level = 0;

  if (action == BACKLIGHT_RETAIN) {
    // We expect the TPS61043_TIM to be already initialized
    // (e.g. by the bootloader or boardloader)
    uint32_t prev_arr = TPS61043_TIM->ARR;
    uint32_t prev_ccr1 = TPS61043_TIM->TPS61043_TIM_CCR;

    initial_level = (prev_ccr1 * 255) / (prev_arr + 1);
    if (initial_level > 255) {
      initial_level = 255;
    }
  }

  // Initialize PWM GPIO
  TPS61043_PORT_CLK_EN();

  GPIO_InitTypeDef GPIO_InitStructure = {0};
  GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStructure.Pull = GPIO_NOPULL;
  GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStructure.Alternate = TPS61043_TIM_AF;
  GPIO_InitStructure.Pin = TPS61043_PIN;
  HAL_GPIO_Init(TPS61043_PORT, &GPIO_InitStructure);

  // Initialize PWM timer
  TPS61043_TIM_FORCE_RESET();
  TPS61043_TIM_RELEASE_RESET();
  TPS61043_TIM_CLK_EN();

  uint32_t tmpcr1 = 0;

  // Select the Counter Mode
  tmpcr1 |= TIM_COUNTERMODE_UP;

  // Set the clock division
  tmpcr1 |= (uint32_t)TIM_CLOCKDIVISION_DIV1;

  // Set the auto-reload preload
#ifdef STM32U5
  tmpcr1 |= TIM_AUTORELOAD_PRELOAD_DISABLE;
#endif

  TPS61043_TIM->CR1 = tmpcr1;

  // Set the Autoreload value
  TPS61043_TIM->ARR = (uint32_t)LED_PWM_TIM_PERIOD - 1;

  // Set the Prescaler value
  TPS61043_TIM->PSC = LED_PWM_PRESCALER;

  // Set the Repetition Counter value
  TPS61043_TIM->RCR = 0;

  // Generate an update event to reload the Prescaler
  // and the repetition counter (only for advanced timer) value immediately
  TPS61043_TIM->EGR = TIM_EGR_UG;

  // Set the Preload enable bit for channel1
  TPS61043_TIM->CCMR1 |= TIM_CCMR1_OC1PE;

  // Configure the Output Fast mode
  TPS61043_TIM->CCMR1 &= ~TIM_CCMR1_OC1FE;
  TPS61043_TIM->CCMR1 |= TIM_OCFAST_DISABLE;

  uint32_t tmpccmrx;
  uint32_t tmpccer;
  uint32_t tmpcr2;

  // Get the TIMx CCER register value
  tmpccer = TPS61043_TIM->CCER;

  // Disable the Channel 1: Reset the CC1E Bit
  TPS61043_TIM->CCER &= ~TIM_CCER_CC1E;
  tmpccer |= TIM_CCER_CC1E;

  // Get the TIMx CR2 register value
  tmpcr2 = TPS61043_TIM->CR2;

  // Get the TIMx CCMR1 register value
  tmpccmrx = TPS61043_TIM->CCMR1;

  // Reset the Output Compare Mode Bits
  tmpccmrx &= ~TIM_CCMR1_OC1M;
  tmpccmrx &= ~TIM_CCMR1_CC1S;
  // Select the Output Compare Mode
  tmpccmrx |= TPS61043_TIM_OCMODE;

  // Reset the Output Polarity level
  tmpccer &= ~TIM_CCER_CC1P;
  // Set the Output Compare Polarity
  tmpccer |= TIM_OCPOLARITY_HIGH;

  if (IS_TIM_CCXN_INSTANCE(TPS61043_TIM, TIM_CHANNEL_1)) {
    // Check parameters
    assert_param(IS_TIM_OCN_POLARITY(OC_Config->OCNPolarity));

    // Reset the Output N Polarity level
    tmpccer &= ~TIM_CCER_CC1NP;
    // Set the Output N Polarity
    tmpccer |= TIM_OCNPOLARITY_HIGH;
    // Set the Output N State
    tmpccer |= TIM_CCER_CC1NE;
  }

  if (IS_TIM_BREAK_INSTANCE(TPS61043_TIM)) {
    // Check parameters
    assert_param(IS_TIM_OCNIDLE_STATE(OC_Config->OCNIdleState));
    assert_param(IS_TIM_OCIDLE_STATE(OC_Config->OCIdleState));

    // Reset the Output Compare and Output Compare N IDLE State
    tmpcr2 &= ~TIM_CR2_OIS1;
    tmpcr2 &= ~TIM_CR2_OIS1N;
    // Set the Output Idle state
    tmpcr2 |= TIM_OCIDLESTATE_SET;
    // Set the Output N Idle state
    tmpcr2 |= TIM_OCNIDLESTATE_SET;
  }

  // Write to TIMx CR2
  TPS61043_TIM->CR2 = tmpcr2;
  // Write to TIMx CCMR1
  TPS61043_TIM->CCMR1 = tmpccmrx;
  // Set the Capture Compare Register value
  TPS61043_TIM->CCR1 = 0;
  // Write to TIMx CCER
  TPS61043_TIM->CCER = tmpccer;

  TPS61043_TIM->BDTR |= TIM_BDTR_MOE;
  TPS61043_TIM->CR1 |= TIM_CR1_CEN;

  drv->initialized = true;

  backlight_set(initial_level);
}

void backlight_deinit(backlight_action_t action) {
  backlight_driver_t *drv = &g_backlight_driver;

  if (!drv->initialized) {
    return;
  }

#ifdef TREZOR_MODEL_T2T1
  // This code is for backward compatibility with the older
  // bootloader/firmware on model T that used different
  // PWM settings and relies on proper settings from the
  // previous stage during boot.

// about 10Hz (with PSC = (SystemCoreClock / 1000000) - 1)
#define LED_PWM_SLOW_TIM_PERIOD (10000)
#define LED_PWM_PRESCALER_SLOW (SystemCoreClock / 1000000 - 1)  // 1 MHz

  TPS61043_TIM->PSC = LED_PWM_PRESCALER_SLOW;
  TPS61043_TIM->CR1 |= TIM_CR1_ARPE;
  TPS61043_TIM->CR2 |= TIM_CR2_CCPC;
  TPS61043_TIM->ARR = LED_PWM_SLOW_TIM_PERIOD - 1;

  if (action == BACKLIGHT_RESET) {
    TPS61043_TIM->TPS61043_TIM_CCR = 0;
  } else {  // action == BACKLIGHT_RETAIN
    TPS61043_TIM->TPS61043_TIM_CCR =
        (LED_PWM_SLOW_TIM_PERIOD * drv->current_level) / 255;
  }

#else
  if (action == BACKLIGHT_RESET) {
    // Deinitialize PWM GPIO
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStructure.Pull = GPIO_NOPULL;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStructure.Pin = TPS61043_PIN;
    HAL_GPIO_Init(TPS61043_PORT, &GPIO_InitStructure);

    // Deinitialize PWM timer
    TPS61043_TIM_FORCE_RESET();
    TPS61043_TIM_RELEASE_RESET();
    TPS61043_TIM_CLK_DIS();

  } else {  // action == BACKLIGHT_RETAIN
    TPS61043_TIM->TPS61043_TIM_CCR =
        (LED_PWM_TIM_PERIOD * drv->current_level) / 255;
  }
#endif

  drv->initialized = false;
}

// Generate a pulse on the backlight control pin to wake up the TPS61043
static void backlight_wakeup_pulse(void) {
  GPIO_InitTypeDef GPIO_InitStructure = {0};

  HAL_GPIO_WritePin(TPS61043_PORT, TPS61043_PIN, GPIO_PIN_SET);

  GPIO_InitStructure.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStructure.Pull = GPIO_NOPULL;
  GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStructure.Pin = TPS61043_PIN;
  HAL_GPIO_Init(TPS61043_PORT, &GPIO_InitStructure);

  hal_delay_us(500);

  GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStructure.Pull = GPIO_NOPULL;
  GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStructure.Alternate = TPS61043_TIM_AF;
  GPIO_InitStructure.Pin = TPS61043_PIN;
  HAL_GPIO_Init(TPS61043_PORT, &GPIO_InitStructure);
}

int backlight_set(int level) {
  backlight_driver_t *drv = &g_backlight_driver;

  if (!drv->initialized) {
    return 0;
  }

  if (level >= 0 && level <= 255) {
    // TPS61043 goes to shutdown when duty cycle is 0 (after 32ms),
    // so we need to set GPIO to high for at least 500us
    // to wake it up.
    if (TPS61043_TIM->TPS61043_TIM_CCR == 0 && level != 0) {
      backlight_wakeup_pulse();
    }

    TPS61043_TIM->CCR1 = (LED_PWM_TIM_PERIOD * level) / 255;

    drv->current_level = level;
  }

  return drv->current_level;
}

int backlight_get(void) {
  backlight_driver_t *drv = &g_backlight_driver;

  if (!drv->initialized) {
    return 0;
  }

  return drv->current_level;
}
