/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/events/sensor_event.h>
#include <zmk/hid_indicators_types.h>
#include <zmk/sensors.h>
#include <zmk/ble.h>

#define ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN 9

struct sensor_event {
    uint8_t sensor_index;

    uint8_t channel_data_size;
    struct zmk_sensor_channel_data channel_data[ZMK_SENSOR_EVENT_MAX_CHANNELS];
} __packed;

struct zmk_split_run_behavior_data {
    uint8_t position;
    uint8_t source;
    uint8_t state;
    uint32_t param1;
    uint32_t param2;
} __packed;

struct zmk_split_run_behavior_payload {
    struct zmk_split_run_behavior_data data;
    char behavior_dev[ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN];
} __packed;

struct zmk_split_input_event_payload {
    uint8_t type;
    uint16_t code;
    uint32_t value;
    uint8_t sync;
} __packed;

/* GLOVE80_DONGLE: Dedicated central USB status channel */
struct zmk_split_central_usb_status_payload {
    uint8_t central_usb_state;
    uint8_t endpoint_is_usb;
} __packed;

/* GLOVE80_DONGLE: Dedicated central BLE status channel */
struct zmk_split_central_ble_status_payload {
    uint8_t active_ble_profile;
    uint8_t profile_count;
    /* Followed on-wire by profile_count bytes of BLE profile state data. */
} __packed;

/* GLOVE80_DONGLE: Dedicated central layer status channel */
struct zmk_split_central_layer_status_payload {
    uint32_t active_layers_mask;
} __packed;
