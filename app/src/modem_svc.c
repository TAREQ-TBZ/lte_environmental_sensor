/*
 * Copyright (c) 2024 Tareq Mhisen
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>

#include "modem_svc.h"

LOG_MODULE_REGISTER(modem_svc, LOG_LEVEL_INF);

/* Semaphore used to block the main thread until modem has established an LTE
 * connection.
 */
static K_SEM_DEFINE(lte_connected, 0, 1);

static void lte_handler(const struct lte_lc_evt *const evt) {
  switch (evt->type) {
  case LTE_LC_EVT_NW_REG_STATUS:
    if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
        (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
      break;
    }
    LOG_INF("Network registration status: %s",
            evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME
                ? "Connected - home network"
                : "Connected - roaming");
    k_sem_give(&lte_connected);
    break;

  case LTE_LC_EVT_RRC_UPDATE:
    LOG_INF("RRC mode: %s",
            evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
    break;

  case LTE_LC_EVT_PSM_UPDATE:
    LOG_INF("PSM update: TAU interval: %d sec, Active time: %d sec",
            evt->psm_cfg.tau, evt->psm_cfg.active_time);
    break;

  case LTE_LC_EVT_MODEM_SLEEP_ENTER:
    LOG_INF("Modem has entered sleep mode!");
    break;

  case LTE_LC_EVT_MODEM_SLEEP_EXIT:
    LOG_INF("Modem has exited sleep mode!");
    break;

  default:
    break;
  }
}

int modem_svc_init(void) {
  int ret;

  ret = nrf_modem_lib_init();
  if (ret != 0) {
    LOG_ERR("Failed to initialize the modem library: %d", ret);
    return ret;
  }

  ret = lte_lc_connect_async(lte_handler);
  if (ret != 0) {
    LOG_ERR("Failed to connect to LTE network: %d", ret);
    return ret;
  }

  k_sem_take(&lte_connected, K_FOREVER);
  LOG_INF("Connected to LTE network");

  return 0;
}
