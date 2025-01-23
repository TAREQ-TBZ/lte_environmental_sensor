/*
 * Copyright (c) 2024 Tareq Mhisen
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "coap_client.h"
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(coap_client, LOG_LEVEL_INF);

#define APP_COAP_VERSION 1
#define APP_COAP_MAX_MSG_LEN 1280
#define DEVICE_TOKEN "qqvdy68yywn6x6q5pfw6"
#define COAP_PATH "api/v1/" DEVICE_TOKEN "/telemetry"

static uint8_t coap_buf[APP_COAP_MAX_MSG_LEN];
static uint16_t next_token;
static int sock;
static struct sockaddr_storage server;

static int configure_socket_timeout(int sock, int timeout_sec,
                                    int timeout_usec) {
  struct timeval timeout = {.tv_sec = timeout_sec, .tv_usec = timeout_usec};

  int ret =
      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  if (ret < 0) {
    LOG_ERR("Failed to set socket timeout: %d", errno);
    return -errno;
  }

  LOG_INF("Socket timeout configured: %d seconds, %d microseconds", timeout_sec,
          timeout_usec);
  return 0;
}

static int resolve_server(void) {
  int ret;
  struct addrinfo *result;
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};

  ret = getaddrinfo(CONFIG_COAP_SERVER_HOSTNAME, NULL, &hints, &result);
  if (ret != 0) {
    LOG_ERR("Failed to resolve server address: %d", ret);
    return -EIO;
  }

  if (!result) {
    LOG_ERR("No address found for server");
    return -ENOENT;
  }

  struct sockaddr_in *server4 = (struct sockaddr_in *)&server;
  server4->sin_addr.s_addr =
      ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
  server4->sin_family = AF_INET;
  server4->sin_port = htons(CONFIG_COAP_SERVER_PORT);

  char ipv4_addr[NET_IPV4_ADDR_LEN];
  inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr, sizeof(ipv4_addr));
  LOG_INF("Resolved IPv4 Address: %s", ipv4_addr);

  freeaddrinfo(result);
  return 0;
}

static int connect_to_server(void) {
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    LOG_ERR("Failed to create socket: %d", errno);
    return -errno;
  }

  if (connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) <
      0) {
    LOG_ERR("Failed to connect to server: %d", errno);
    return -errno;
  }

  LOG_INF("Connected to server");
  next_token = sys_rand32_get();
  return 0;
}

static int prepare_coap_request(struct coap_packet *request, uint8_t method,
                                const char *path, const uint8_t *payload,
                                size_t payload_len) {
  int ret = coap_packet_init(
      request, coap_buf, sizeof(coap_buf), APP_COAP_VERSION, COAP_TYPE_NON_CON,
      sizeof(next_token), (uint8_t *)&next_token, method, coap_next_id());
  if (ret < 0) {
    LOG_ERR("Failed to initialize CoAP packet: %d", ret);
    return ret;
  }

  ret = coap_packet_set_path(request, path);
  if (ret < 0) {
    LOG_ERR("Failed to set URI path: %d", ret);
    return ret;
  }

  if (payload && payload_len > 0) {
    ret =
        coap_packet_append_option(request, COAP_OPTION_CONTENT_FORMAT,
                                  &(uint8_t){COAP_CONTENT_FORMAT_APP_JSON}, 1);
    if (ret < 0) {
      LOG_ERR("Failed to set content format: %d", ret);
      return ret;
    }

    ret = coap_packet_append_payload_marker(request);
    if (ret < 0) {
      LOG_ERR("Failed to append payload marker: %d", ret);
      return ret;
    }

    ret = coap_packet_append_payload(request, payload, payload_len);
    if (ret < 0) {
      LOG_ERR("Failed to append payload: %d", ret);
      return ret;
    }
  }

  return 0;
}

static int handle_response(uint8_t *buf, int received) {
  struct coap_packet reply;
  uint8_t token[8];
  uint16_t token_len;
  const uint8_t *payload;
  uint16_t payload_len;

  int ret = coap_packet_parse(&reply, buf, received, NULL, 0);
  if (ret < 0) {
    LOG_ERR("Malformed response received: %d", ret);
    return ret;
  }

  token_len = coap_header_get_token(&reply, token);
  if (token_len != sizeof(next_token) ||
      memcmp(&next_token, token, sizeof(next_token)) != 0) {
    LOG_ERR("Invalid token received");
    return -EINVAL;
  }

  payload = coap_packet_get_payload(&reply, &payload_len);
  if (payload_len > 0) {
    LOG_INF("CoAP response payload: %.*s", payload_len, (const char *)payload);
  } else {
    LOG_INF("CoAP response payload: EMPTY");
  }

  return 0;
}

static int send_coap_request_and_wait(uint8_t method, const char *path,
                                      const uint8_t *payload,
                                      size_t payload_len) {
  struct coap_packet request;
  int ret, received;

  next_token = sys_rand32_get();
  ret = prepare_coap_request(&request, method, path, payload, payload_len);
  if (ret < 0) {
    return ret;
  }

  ret = send(sock, request.data, request.offset, 0);
  if (ret < 0) {
    LOG_ERR("Failed to send CoAP request: %d", errno);
    return -errno;
  }

  LOG_INF("CoAP request sent: Token 0x%04x", next_token);

  received = recv(sock, coap_buf, sizeof(coap_buf), 0);
  if (received < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      LOG_ERR("Receive timeout occurred");
      return -ETIMEDOUT;
    } else {
      LOG_ERR("Failed to receive CoAP response: %d", errno);
      return -errno;
    }
  } else if (received == 0) {
    LOG_INF("Empty datagram received");
    return 0;
  }

  ret = handle_response(coap_buf, received);
  if (ret < 0) {
    LOG_ERR("Failed to handle response: %d", ret);
  }

  return ret;
}

int coap_client_send_env_data(int temperature, int humidity) {
  char telemetry_payload[128];

  snprintf(telemetry_payload, sizeof(telemetry_payload),
           "{\"temperature\": %d, \"humidity\": %d}", temperature, humidity);

  int ret = send_coap_request_and_wait(COAP_METHOD_POST, COAP_PATH,
                                       (uint8_t *)telemetry_payload,
                                       strlen(telemetry_payload));
  if (ret < 0) {
    LOG_ERR("POST request failed: %d", ret);
    return ret;
  }
  return 0;
}

int coap_client_init(void) {
  int ret;

  ret = resolve_server();
  if (ret != 0) {
    return ret;
  }

  ret = connect_to_server();
  if (ret != 0) {
    return ret;
  }

  ret = configure_socket_timeout(sock, 5, 0);
  if (ret != 0) {
    return ret;
  }

  LOG_INF("CoAP client initialized");
  return 0;
}
