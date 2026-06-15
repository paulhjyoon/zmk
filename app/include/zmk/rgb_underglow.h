/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <zmk/hid_indicators_types.h>
#include <zmk/usb.h>

struct zmk_led_hsb {
    uint16_t h;
    uint8_t s;
    uint8_t b;
};

int zmk_rgb_underglow_toggle(void);
int zmk_rgb_underglow_get_state(bool *state);
int zmk_rgb_underglow_on(void);
int zmk_rgb_underglow_off(void);
int zmk_rgb_underglow_cycle_effect(int direction);
int zmk_rgb_underglow_calc_effect(int direction);
int zmk_rgb_underglow_select_effect(int effect);
struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction);
struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction);
int zmk_rgb_underglow_change_hue(int direction);
int zmk_rgb_underglow_change_sat(int direction);
int zmk_rgb_underglow_change_brt(int direction);
int zmk_rgb_underglow_change_spd(int direction);
int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color);
int zmk_rgb_underglow_status(void);
int zmk_rgb_underglow_status_duration_ms(void);

/* GLOVE80_DONGLE: Peripheral cache update hooks for split-delivered status. */
void zmk_rgb_underglow_set_cached_hid_indicators(zmk_hid_indicators_t indicators);
void zmk_rgb_underglow_set_cached_ble_status(uint8_t active_ble_profile,
                                             uint8_t profile_count,
                                             const uint8_t *ble_profile_states,
                                             size_t ble_profile_states_len);
void zmk_rgb_underglow_set_cached_usb_status(enum zmk_usb_conn_state central_usb_state,
                                             bool endpoint_is_usb);
void zmk_rgb_underglow_set_cached_layer_status(uint32_t active_layers_mask);
