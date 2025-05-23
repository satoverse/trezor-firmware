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

#include <sys/powerctl.h>
#include <sys/systick.h>

#include "../npm1300/npm1300.h"
#include "../stwlc38/stwlc38.h"

#ifdef KERNEL_MODE

// Power control driver state
typedef struct {
  // True if the driver is initialized
  bool initialized;

} powerctl_driver_t;

// Power control driver instance
static powerctl_driver_t g_powerctl_driver = {.initialized = false};

bool powerctl_init(void) {
  powerctl_driver_t* drv = &g_powerctl_driver;

  if (drv->initialized) {
    return true;
  }

  // Initialize PMIC
  if (!npm1300_init()) {
    goto cleanup;
  }

  // Initialize wireless charging
  if (!stwlc38_init()) {
    goto cleanup;
  }

  drv->initialized = true;

  return true;

cleanup:
  stwlc38_deinit();
  npm1300_deinit();
  return false;
}

void powerctl_deinit(void) {
  powerctl_driver_t* drv = &g_powerctl_driver;

  if (!drv->initialized) {
    return;
  }

  stwlc38_deinit();
  npm1300_deinit();

  drv->initialized = false;
}

bool powerctl_get_status(powerctl_status_t* status) {
  powerctl_driver_t* drv = &g_powerctl_driver;

  memset(status, 0, sizeof(powerctl_status_t));

  if (!drv->initialized) {
    return false;
  }

  // TODO

  return true;
}

bool powerctl_hibernate(void) {
  powerctl_driver_t* drv = &g_powerctl_driver;

  if (!drv->initialized) {
    return false;
  }

  // TEMPORARY FIX:
  // Enable Backup domain retentaion in VBAT mode before entering the
  // hiberbation. BREN bit can be accessed only in LDO mode.
  __HAL_RCC_PWR_CLK_ENABLE();

  // Switch to LDO regulator
  CLEAR_BIT(PWR->CR3, PWR_CR3_REGSEL);
  // Wait until system switch on new regulator
  while (HAL_IS_BIT_SET(PWR->SVMSR, PWR_SVMSR_REGS))
    ;
  // Enable backup domain retention
  PWR->BDCR1 |= PWR_BDCR1_BREN;

  if (!npm1300_enter_shipmode()) {
    return false;
  }

  // Wait for the device to power off
  systick_delay_ms(50);

  return true;
}

#endif  // KERNEL_MODE
