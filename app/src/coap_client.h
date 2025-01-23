/*
 * Copyright (c) 2024 Tareq Mhisen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_COAP_CLIENT_H_
#define APP_COAP_CLIENT_H_

int coap_client_init(void);
int coap_client_send_env_data(int temperature, int humidity);

#endif /* APP_COAP_CLIENT_H_ */
