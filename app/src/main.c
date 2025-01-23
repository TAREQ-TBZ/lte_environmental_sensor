/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_client.h"
#include "modem_svc.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static struct k_work_delayable telemetry_work;

static int temperature;
static int humidity = 20;

static void telemetry_work_handler(struct k_work *work) {
  if (temperature < 40) {
    temperature++;
  } else {
    temperature = 0;
  }

  if (humidity < 60) {
    humidity++;
  } else {
    humidity = 20;
  }

  coap_client_send_env_data(temperature, humidity);

  k_work_reschedule(&telemetry_work, K_SECONDS(30));
}

int main(void) {
  int ret;

  LOG_INF("Starting up .. .. ..");
  k_work_init_delayable(&telemetry_work, telemetry_work_handler);

  ret = modem_svc_init();
  if (ret != 0) {
    LOG_ERR("Failed to initialize the modem service!");
  }

  ret = coap_client_init();
  if (ret != 0) {
    LOG_ERR("Failed to initialize the coap client!");
  }

  k_work_schedule(&telemetry_work, K_SECONDS(5));

  return 0;
}
