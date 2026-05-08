/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <math.h>
#include <stdlib.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/hid_indicators.h>
#include <zmk/usb.h>

#include <zephyr/logging/log.h>

#include <zephyr/drivers/led_strip.h>
#include <drivers/ext_power.h>

#include <zmk/rgb_underglow.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/workqueue.h>

#include <zmk/split/bluetooth/service.h>
#include <stdint.h>
#include <string.h>

#if DT_HAS_CHOSEN(zmk_underglow_indicators)
#define UNDERGLOW_INDICATORS DT_CHOSEN(zmk_underglow_indicators)
#else
#define UNDERGLOW_INDICATORS DT_PATH(underglow_indicators)
#endif

#if DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, ble_state)
#define ZMK_RGB_UNDERGLOW_BLE_CACHE_SIZE DT_PROP_LEN(UNDERGLOW_INDICATORS, ble_state)
#else
#define ZMK_RGB_UNDERGLOW_BLE_CACHE_SIZE 0
#endif

/* GLOVE80_DONGLE: Indicator cache helpers (must be declared before use) */
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static zmk_hid_indicators_t cached_hid_indicators;
static uint32_t cached_active_layers_mask;
static uint8_t cached_ble_profile;
static uint8_t cached_ble_profile_count;
static uint8_t cached_ble_profile_states[MAX(1, ZMK_RGB_UNDERGLOW_BLE_CACHE_SIZE)];
static bool cached_endpoint_is_usb;
static enum zmk_usb_conn_state cached_central_usb_state = ZMK_USB_CONN_NONE;

static inline zmk_hid_indicators_t zmk_cached_hid_indicators(void) { return cached_hid_indicators; }

static inline bool zmk_cached_layer_active(uint8_t layer) {
    if (layer >= 32) {
        return false;
    }
    return (cached_active_layers_mask & BIT(layer)) != 0;
}

/* GLOVE80_DONGLE: Cache shim signature matches no-arg callsites. */
static inline bool zmk_cached_endpoint_is_active(void) {
    /*
     * "Selected endpoint is USB" != "selected endpoint is active".
     * Use cached central status for peripheral-side rendering.
     */
    if (cached_endpoint_is_usb) {
        return cached_central_usb_state == ZMK_USB_CONN_HID;
    }

    /*
     * BLE profile status:
     * 0 = disconnected, non-zero = connected/active-ish.
     */
    if (cached_ble_profile < ARRAY_SIZE(cached_ble_profile_states)) {
        return cached_ble_profile_states[cached_ble_profile] != 0;
    }
    return false;
}

static inline uint8_t zmk_cached_ble_profile_index(void) { return cached_ble_profile; }

static inline uint8_t zmk_cached_ble_profile_status(uint8_t profile) {
    if (profile >= ARRAY_SIZE(cached_ble_profile_states)) {
        return 0;
    }

    return cached_ble_profile_states[profile];
}

/* GLOVE80_DONGLE: Central USB state delivered via split transport into cache. */
static inline enum zmk_usb_conn_state zmk_cached_central_usb_state(void) {
    return cached_central_usb_state;
}

static inline bool zmk_cached_endpoint_is_usb_selected(void) { return cached_endpoint_is_usb; }
#else

static inline zmk_hid_indicators_t zmk_cached_hid_indicators(void) {
    return zmk_hid_indicators_get_current_profile();
}

static inline bool zmk_cached_layer_active(uint8_t layer) { return zmk_keymap_layer_active(layer); }

/* GLOVE80_DONGLE: Keep no-arg signature used by transformed callsites. */
static inline bool zmk_cached_endpoint_is_active(void) {
    struct zmk_endpoint_instance selected = zmk_endpoints_selected();
    return zmk_endpoints_preferred_transport_is_active(selected.transport);
}

static inline uint8_t zmk_cached_ble_profile_index(void) { return zmk_ble_active_profile_index(); }

static inline uint8_t zmk_cached_ble_profile_status(uint8_t profile) {
    return zmk_ble_profile_status(profile);
}

/* GLOVE80_DONGLE: On central-capable builds, central USB is the local USB state. */
static inline enum zmk_usb_conn_state zmk_cached_central_usb_state(void) {
    return zmk_usb_get_conn_state();
}

static inline bool zmk_cached_endpoint_is_usb_selected(void) {
    struct zmk_endpoint_instance selected = zmk_endpoints_selected();
    return selected.transport == ZMK_TRANSPORT_USB;
}
#endif /* CONFIG_ZMK_SPLIT && !CONFIG_ZMK_SPLIT_ROLE_CENTRAL */

void zmk_rgb_underglow_set_cached_hid_indicators(zmk_hid_indicators_t indicators) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    cached_hid_indicators = indicators;
#else
    ARG_UNUSED(indicators);
#endif
}

void zmk_rgb_underglow_set_cached_ble_status(uint8_t active_ble_profile, uint8_t profile_count,
                                             const uint8_t *ble_profile_states,
                                             size_t ble_profile_states_len) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    cached_ble_profile = active_ble_profile;
    cached_ble_profile_count = MIN(profile_count, (uint8_t)ZMK_RGB_UNDERGLOW_BLE_CACHE_SIZE);

    memset(cached_ble_profile_states, 0, sizeof(cached_ble_profile_states));

    if (ble_profile_states && ble_profile_states_len > 0) {
        size_t copy_len = MIN(ble_profile_states_len, (size_t)cached_ble_profile_count);
        memcpy(cached_ble_profile_states, ble_profile_states, copy_len);
    }
#else
    ARG_UNUSED(active_ble_profile);
    ARG_UNUSED(profile_count);
    ARG_UNUSED(ble_profile_states);
    ARG_UNUSED(ble_profile_states_len);
#endif
}

void zmk_rgb_underglow_set_cached_usb_status(enum zmk_usb_conn_state central_usb_state,
                                             bool endpoint_is_usb) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    cached_central_usb_state = central_usb_state;
    cached_endpoint_is_usb = endpoint_is_usb;
#else
    ARG_UNUSED(central_usb_state);
    ARG_UNUSED(endpoint_is_usb);
#endif
}

void zmk_rgb_underglow_set_cached_layer_status(uint32_t active_layers_mask) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    cached_active_layers_mask = active_layers_mask;
#else
    ARG_UNUSED(active_layers_mask);
#endif
}

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_underglow)

#error "A zmk,underglow chosen node must be declared"

#endif

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)

#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100

BUILD_ASSERT(CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN <= CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX,
             "ERROR: RGB underglow maximum brightness is less than minimum brightness");

enum rgb_underglow_effect {
    UNDERGLOW_EFFECT_SOLID,
    UNDERGLOW_EFFECT_BREATHE,
    UNDERGLOW_EFFECT_SPECTRUM,
    UNDERGLOW_EFFECT_SWIRL,
    UNDERGLOW_EFFECT_NUMBER // Used to track number of underglow effects
};

struct rgb_underglow_state {
    struct zmk_led_hsb color;
    uint8_t animation_speed;
    uint8_t current_effect;
    uint16_t animation_step;
    bool on;
    bool status_active;
    uint16_t status_animation_step;
};

static const struct device *led_strip;

static struct led_rgb pixels[STRIP_NUM_PIXELS];
static struct led_rgb status_pixels[STRIP_NUM_PIXELS];

static struct rgb_underglow_state state;

static inline bool zmk_rgb_underglow_should_render_local(void) {
    return !(IS_ENABLED(CONFIG_ZMK_SPLIT) && IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL));
}

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
static const struct device *const ext_power = DEVICE_DT_GET(DT_INST(0, zmk_ext_power_generic));
#endif

void zmk_rgb_set_ext_power(void);

static struct zmk_led_hsb hsb_scale_min_max(struct zmk_led_hsb hsb) {
    hsb.b = CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN +
            (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX - CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN) * hsb.b / BRT_MAX;
    return hsb;
}

static struct zmk_led_hsb hsb_scale_zero_max(struct zmk_led_hsb hsb) {
    hsb.b = hsb.b * CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX / BRT_MAX;
    return hsb;
}

static struct led_rgb hsb_to_rgb(struct zmk_led_hsb hsb) {
    float r = 0, g = 0, b = 0;

    uint8_t i = hsb.h / 60;
    float v = hsb.b / ((float)BRT_MAX);
    float s = hsb.s / ((float)SAT_MAX);
    float f = hsb.h / ((float)HUE_MAX) * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
    case 0:
        r = v;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = v;
        b = p;
        break;
    case 2:
        r = p;
        g = v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = v;
        break;
    case 4:
        r = t;
        g = p;
        b = v;
        break;
    case 5:
        r = v;
        g = p;
        b = q;
        break;
    }

    struct led_rgb rgb = {r : r * 255, g : g * 255, b : b * 255};

    return rgb;
}

static void zmk_rgb_underglow_effect_solid(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = hsb_to_rgb(hsb_scale_min_max(state.color));
    }
}

static void zmk_rgb_underglow_effect_breathe(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        struct zmk_led_hsb hsb = state.color;
        hsb.b = abs(state.animation_step - 1200) / 12;

        pixels[i] = hsb_to_rgb(hsb_scale_zero_max(hsb));
    }

    state.animation_step += state.animation_speed * 10;

    if (state.animation_step > 2400) {
        state.animation_step = 0;
    }
}

static void zmk_rgb_underglow_effect_spectrum(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        struct zmk_led_hsb hsb = state.color;
        hsb.h = state.animation_step;

        pixels[i] = hsb_to_rgb(hsb_scale_min_max(hsb));
    }

    state.animation_step += state.animation_speed;
    state.animation_step = state.animation_step % HUE_MAX;
}

static void zmk_rgb_underglow_effect_swirl(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        struct zmk_led_hsb hsb = state.color;
        hsb.h = (HUE_MAX / STRIP_NUM_PIXELS * i + state.animation_step) % HUE_MAX;

        pixels[i] = hsb_to_rgb(hsb_scale_min_max(hsb));
    }

    state.animation_step += state.animation_speed * 2;
    state.animation_step = state.animation_step % HUE_MAX;
}

static int zmk_led_generate_status(void);

static void zmk_led_write_pixels(void) {
    if (!zmk_rgb_underglow_should_render_local()) {
        return;
    }

    static struct led_rgb led_buffer[STRIP_NUM_PIXELS];
    int bat0;
    int blend = 0;
    int reset_ext_power = 0;

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    bat0 = zmk_battery_state_of_charge();
#else
    bat0 = 100;
#endif

    if (state.status_active) {
        blend = zmk_led_generate_status();
    }

    // fast path: no status indicators, battery level OK
    if (blend == 0 && bat0 >= 20) {
        led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
        return;
    }
    // battery below minimum charge
    if (bat0 < 10) {
        memset(pixels, 0, sizeof(struct led_rgb) * STRIP_NUM_PIXELS);
        if (state.on) {
            if (!state.status_active) {
                // power is on, RGB underglow is on, but battery is too low
                state.on = false;
                reset_ext_power = true;
            }
        }
    }

    if (blend == 0) {
        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            led_buffer[i] = pixels[i];
        }
    } else if (blend >= 256) {
        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            led_buffer[i] = status_pixels[i];
        }
    } else if (blend < 256) {
        uint16_t blend_l = blend;
        uint16_t blend_r = 256 - blend;
        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            led_buffer[i].r =
                ((status_pixels[i].r * blend_l) >> 8) + ((pixels[i].r * blend_r) >> 8);
            led_buffer[i].g =
                ((status_pixels[i].g * blend_l) >> 8) + ((pixels[i].g * blend_r) >> 8);
            led_buffer[i].b =
                ((status_pixels[i].b * blend_l) >> 8) + ((pixels[i].b * blend_r) >> 8);
        }
    }

    // battery below 20%, reduce LED brightness
    if (bat0 < 20) {
        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            led_buffer[i].r = led_buffer[i].r >> 1;
            led_buffer[i].g = led_buffer[i].g >> 1;
            led_buffer[i].b = led_buffer[i].b >> 1;
        }
    }

    int err = led_strip_update_rgb(led_strip, led_buffer, STRIP_NUM_PIXELS);
    if (err < 0) {
        LOG_ERR("Failed to update the RGB strip (%d)", err);
    }

    if (reset_ext_power) {
        zmk_rgb_set_ext_power();
    }
}

#if !DT_NODE_EXISTS(DT_PATH(underglow_indicators))
static int zmk_led_generate_status(void) { return 0; }
#else

/* GLOVE80_DONGLE: Always keep bat_lhs enabled */
const uint8_t underglow_bat_lhs[] = DT_PROP(UNDERGLOW_INDICATORS, bat_lhs);

/* GLOVE80_DONGLE: LHS-only DT properties. */
#if !defined(CONFIG_BOARD_GLOVE80_RH) && DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, layer_state)
const uint8_t underglow_layer_state[] = DT_PROP(UNDERGLOW_INDICATORS, layer_state);
#endif
#if !defined(CONFIG_BOARD_GLOVE80_RH) && DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, ble_state)
const uint8_t underglow_ble_state[] = DT_PROP(UNDERGLOW_INDICATORS, ble_state);
#endif
#if !defined(CONFIG_BOARD_GLOVE80_RH) && DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, bat_rhs)
const uint8_t underglow_bat_rhs[] = DT_PROP(UNDERGLOW_INDICATORS, bat_rhs);
#endif

#define HEXRGB(R, G, B)                                                                            \
    ((struct led_rgb){                                                                             \
        r : (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX * (R)) / 0xff,                                       \
        g : (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX * (G)) / 0xff,                                       \
        b : (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX * (B)) / 0xff                                        \
    })
const struct led_rgb red = HEXRGB(0xff, 0x00, 0x00);
const struct led_rgb yellow = HEXRGB(0xff, 0xff, 0x00);
const struct led_rgb green = HEXRGB(0x00, 0xff, 0x00);
const struct led_rgb dull_green = HEXRGB(0x00, 0xff, 0x68);
const struct led_rgb magenta = HEXRGB(0xff, 0x00, 0xff);
const struct led_rgb white = HEXRGB(0xff, 0xff, 0xff);
const struct led_rgb lilac = HEXRGB(0x6b, 0x1f, 0xce);

static void zmk_led_battery_level(int bat_level, const uint8_t *addresses, size_t addresses_len) {
    struct led_rgb bat_colour;

    if (bat_level >= 40) {
        bat_colour = green;
    } else if (bat_level >= 20) {
        bat_colour = yellow;
    } else {
        bat_colour = red;
    }

    // originally, six levels, 0 .. 100

    for (int i = 0; i < addresses_len; i++) {
        int min_level = (i * 100) / (addresses_len - 1);
        if (bat_level >= min_level) {
            status_pixels[addresses[i]] = bat_colour;
        }
    }
}

static void zmk_led_fill(struct led_rgb color, const uint8_t *addresses, size_t addresses_len) {
    for (int i = 0; i < addresses_len; i++) {
        status_pixels[addresses[i]] = color;
    }
}

#define ZMK_LED_NUMLOCK_BIT BIT(0)
#define ZMK_LED_CAPSLOCK_BIT BIT(1)
#define ZMK_LED_SCROLLLOCK_BIT BIT(2)

static int zmk_led_generate_status(void) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        status_pixels[i] = (struct led_rgb){r : 0, g : 0, b : 0};
    }

    // BATTERY STATUS
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    zmk_led_battery_level(zmk_battery_state_of_charge(), underglow_bat_lhs,
                          DT_PROP_LEN(UNDERGLOW_INDICATORS, bat_lhs));
#endif // CONFIG_ZMK_BATTERY_REPORTING

#if !defined(CONFIG_BOARD_GLOVE80_RH)
    /* GLOVE80_DONGLE: Use cached central endpoint selection on peripherals. */
    bool usb_output_active = zmk_cached_endpoint_is_usb_selected();
    // CAPSLOCK/NUMLOCK/SCROLLOCK STATUS
    zmk_hid_indicators_t led_flags = zmk_cached_hid_indicators();

#if DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, capslock)
    if (led_flags & ZMK_LED_CAPSLOCK_BIT)
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, capslock)] = red;
#endif
#if DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, numlock)
    if (led_flags & ZMK_LED_NUMLOCK_BIT)
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, numlock)] = red;
#endif
#if DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, scrolllock)
    if (led_flags & ZMK_LED_SCROLLLOCK_BIT)
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, scrolllock)] = red;
#endif

#if DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, layer_state)
    // LAYER STATUS
    for (uint8_t i = 0; i < DT_PROP_LEN(UNDERGLOW_INDICATORS, layer_state); i++) {
        if (zmk_cached_layer_active(i))
            status_pixels[underglow_layer_state[i]] = magenta;
    }
#endif

#if DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, output_fallback)
    if (!zmk_cached_endpoint_is_active())
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, output_fallback)] = red;
#endif

#if IS_ENABLED(CONFIG_ZMK_BLE) && DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, ble_state)
    int active_ble_profile_index = zmk_cached_ble_profile_index();
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint8_t profile_slots =
        MIN(cached_ble_profile_count, (uint8_t)DT_PROP_LEN(UNDERGLOW_INDICATORS, ble_state));
#else
    uint8_t profile_slots =
        MIN((uint8_t)ZMK_BLE_PROFILE_COUNT, (uint8_t)DT_PROP_LEN(UNDERGLOW_INDICATORS, ble_state));
#endif

    for (uint8_t i = 0; i < profile_slots; i++) {
        int8_t status = zmk_cached_ble_profile_status(i);
        int ble_pixel = underglow_ble_state[i];
        if (status == 2 && !usb_output_active &&
            active_ble_profile_index == i) { // connected AND active
            status_pixels[ble_pixel] = white;
        } else if (status == 2) { // connected
            status_pixels[ble_pixel] = dull_green;
        } else if (status == 1) { // paired
            status_pixels[ble_pixel] = red;
        } else if (status == 0) { // unused
            status_pixels[ble_pixel] = lilac;
        }
    }
#endif // CONFIG_ZMK_BLE && ble_state
#endif // CONFIG_BOARD_GLOVE80_RH

/* GLOVE80_DONGLE: LHS-only indicator for dongle central USB state. */
#if !defined(CONFIG_BOARD_GLOVE80_RH) && DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, central_usb)
    enum zmk_usb_conn_state central_usb_state = zmk_cached_central_usb_state();
    uint8_t central_usb_px = DT_PROP_BY_IDX(UNDERGLOW_INDICATORS, central_usb, 0);
    if (central_usb_state == ZMK_USB_CONN_HID) { // connected
        status_pixels[central_usb_px] = white;
    } else if (central_usb_state == ZMK_USB_CONN_POWERED) { // powered
        status_pixels[central_usb_px] = red;
    } else if (central_usb_state == ZMK_USB_CONN_NONE) { // disconnected
        status_pixels[central_usb_px] = lilac;
    }
#endif

#if DT_NODE_HAS_PROP(UNDERGLOW_INDICATORS, usb_state)
    enum zmk_usb_conn_state usb_state = zmk_usb_get_conn_state();
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    /* Peripheral USB is never used for HID input (CONFIG_ZMK_USB=n).
     * Show: dull_green when enumerated, red when powered, lilac when disconnected.
     */
    if (usb_state == ZMK_USB_CONN_HID) { // connected (enumerated)
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = dull_green;
    } else if (usb_state == ZMK_USB_CONN_POWERED) { // powered
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = red;
    } else if (usb_state == ZMK_USB_CONN_NONE) { // disconnected
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = lilac;
    }
#else
    if (usb_state == ZMK_USB_CONN_HID && usb_output_active) { // connected AND active
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = white;
    } else if (usb_state == ZMK_USB_CONN_HID) { // connected
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = dull_green;
    } else if (usb_state == ZMK_USB_CONN_POWERED) { // powered
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = red;
    } else if (usb_state == ZMK_USB_CONN_NONE) { // disconnected
        status_pixels[DT_PROP(UNDERGLOW_INDICATORS, usb_state)] = lilac;
    }
#endif
#endif

    int16_t blend = 256;
    if (state.status_animation_step < (500 / 25)) {
        blend = ((state.status_animation_step * 256) / (500 / 25));
    } else if (state.status_animation_step > (8000 / 25)) {
        blend = 256 - (((state.status_animation_step - (8000 / 25)) * 256) / (2000 / 25));
    }
    if (blend < 0)
        blend = 0;
    if (blend > 256)
        blend = 256;

    return blend;
}
#endif // underglow_indicators exists

static void zmk_rgb_underglow_tick(struct k_work *work) {
    switch (state.current_effect) {
    case UNDERGLOW_EFFECT_SOLID:
        zmk_rgb_underglow_effect_solid();
        break;
    case UNDERGLOW_EFFECT_BREATHE:
        zmk_rgb_underglow_effect_breathe();
        break;
    case UNDERGLOW_EFFECT_SPECTRUM:
        zmk_rgb_underglow_effect_spectrum();
        break;
    case UNDERGLOW_EFFECT_SWIRL:
        zmk_rgb_underglow_effect_swirl();
        break;
    }

    zmk_led_write_pixels();
}

K_WORK_DEFINE(underglow_tick_work, zmk_rgb_underglow_tick);

static void zmk_rgb_underglow_tick_handler(struct k_timer *timer) {
    if (!state.on) {
        return;
    }

    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_tick_work);
}

K_TIMER_DEFINE(underglow_tick, zmk_rgb_underglow_tick_handler, NULL);

#if IS_ENABLED(CONFIG_SETTINGS)
static int rgb_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    int rc;

    if (settings_name_steq(name, "state", &next) && !next) {
        if (len != sizeof(state)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &state, sizeof(state));
        if (rc >= 0) {
            if (state.on) {
                k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));
            }

            return 0;
        }

        return rc;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(rgb_underglow, "rgb/underglow", NULL, rgb_settings_set, NULL, NULL);

static void zmk_rgb_underglow_save_state_work(struct k_work *_work) {
    settings_save_one("rgb/underglow/state", &state, sizeof(state));
}

static struct k_work_delayable underglow_save_work;
#endif

static int zmk_rgb_underglow_init(void) {
    led_strip = DEVICE_DT_GET(STRIP_CHOSEN);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    if (!device_is_ready(ext_power)) {
        LOG_ERR("External power device \"%s\" is not ready", ext_power->name);
        return -ENODEV;
    }
#endif

    state = (struct rgb_underglow_state){
        color : {
            h : CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
            s : CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
            b : CONFIG_ZMK_RGB_UNDERGLOW_BRT_START,
        },
        animation_speed : CONFIG_ZMK_RGB_UNDERGLOW_SPD_START,
        current_effect : CONFIG_ZMK_RGB_UNDERGLOW_EFF_START,
        animation_step : 0,
        on : IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_ON_START)
    };

#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_init_delayable(&underglow_save_work, zmk_rgb_underglow_save_state_work);
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
    state.on = zmk_usb_is_powered();
#endif

    if (state.on && zmk_rgb_underglow_should_render_local()) {
        k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(25));
    }

    return 0;
}

int zmk_rgb_underglow_save_state(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    int ret = k_work_reschedule(&underglow_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
    return MIN(ret, 0);
#else
    return 0;
#endif
}

int zmk_rgb_underglow_get_state(bool *on_off) {
    if (!led_strip)
        return -ENODEV;

    *on_off = state.on;
    return 0;
}

void zmk_rgb_set_ext_power(void) {
#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    if (ext_power == NULL)
        return;
    int c_power = ext_power_get(ext_power);
    if (c_power < 0) {
        LOG_ERR("Unable to examine EXT_POWER: %d", c_power);
        c_power = 0;
    }
    int desired_state = state.on || state.status_active;

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
    // force power off, when battery low (<10%)
    if (state.on && !state.status_active) {
        if (zmk_battery_state_of_charge() < 10) {
            desired_state = false;
        }
    }
#endif // CONFIG_ZMK_BATTERY_REPORTING

    if (desired_state && !c_power) {
        int rc = ext_power_enable(ext_power);
        if (rc != 0) {
            LOG_ERR("Unable to enable EXT_POWER: %d", rc);
        }
    } else if (!desired_state && c_power) {
        int rc = ext_power_disable(ext_power);
        if (rc != 0) {
            LOG_ERR("Unable to disable EXT_POWER: %d", rc);
        }
    }
#endif // CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER
}

int zmk_rgb_underglow_on(void) {
    if (!led_strip)
        return -ENODEV;

    state.on = true;
    zmk_rgb_set_ext_power();

    state.animation_step = 0;
    if (zmk_rgb_underglow_should_render_local()) {
        k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(25));
    }

    return zmk_rgb_underglow_save_state();
}

static void zmk_rgb_underglow_off_handler(struct k_work *work) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = (struct led_rgb){r : 0, g : 0, b : 0};
    }

    zmk_led_write_pixels();
}

K_WORK_DEFINE(underglow_off_work, zmk_rgb_underglow_off_handler);

int zmk_rgb_underglow_off(void) {
    if (!led_strip)
        return -ENODEV;

    if (zmk_rgb_underglow_should_render_local()) {
        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_off_work);
    }

    k_timer_stop(&underglow_tick);
    state.on = false;
    zmk_rgb_set_ext_power();

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_calc_effect(int direction) {
    return (state.current_effect + UNDERGLOW_EFFECT_NUMBER + direction) % UNDERGLOW_EFFECT_NUMBER;
}

int zmk_rgb_underglow_select_effect(int effect) {
    if (!led_strip)
        return -ENODEV;

    if (effect < 0 || effect >= UNDERGLOW_EFFECT_NUMBER) {
        return -EINVAL;
    }

    state.current_effect = effect;
    state.animation_step = 0;

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_cycle_effect(int direction) {
    return zmk_rgb_underglow_select_effect(zmk_rgb_underglow_calc_effect(direction));
}

int zmk_rgb_underglow_toggle(void) {
    return state.on ? zmk_rgb_underglow_off() : zmk_rgb_underglow_on();
}

static void zmk_led_write_pixels_work(struct k_work *work);
static void zmk_rgb_underglow_status_update(struct k_timer *timer);

K_WORK_DEFINE(underglow_write_work, zmk_led_write_pixels_work);
K_TIMER_DEFINE(underglow_status_update_timer, zmk_rgb_underglow_status_update, NULL);

static void zmk_rgb_underglow_status_update(struct k_timer *timer) {
    if (!state.status_active)
        return;
    state.status_animation_step++;
    if (state.status_animation_step > (10000 / 25)) {
        state.status_active = false;
        k_timer_stop(&underglow_status_update_timer);
    }
    if (!k_work_is_pending(&underglow_write_work)) {
        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_write_work);
    }
}

static void zmk_led_write_pixels_work(struct k_work *work) {
    zmk_led_write_pixels();
    if (!state.status_active) {
        zmk_rgb_set_ext_power();
    }
}

int zmk_rgb_underglow_status(void) {
    if (!zmk_rgb_underglow_should_render_local()) {
        state.status_active = false;
        return 0;
    }

    if (!state.status_active) {
        state.status_animation_step = 0;
    } else {
        if (state.status_animation_step > (500 / 25)) {
            state.status_animation_step = 500 / 25;
        }
    }
    state.status_active = true;
    if (!k_work_is_pending(&underglow_write_work)) {
        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &underglow_write_work);
    }
    zmk_rgb_set_ext_power();

    k_timer_start(&underglow_status_update_timer, K_NO_WAIT, K_MSEC(25));

    return 0;
}

int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    if (color.h > HUE_MAX || color.s > SAT_MAX || color.b > BRT_MAX) {
        return -ENOTSUP;
    }

    state.color = color;

    return 0;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction) {
    struct zmk_led_hsb color = state.color;

    color.h += HUE_MAX + (direction * CONFIG_ZMK_RGB_UNDERGLOW_HUE_STEP);
    color.h %= HUE_MAX;

    return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction) {
    struct zmk_led_hsb color = state.color;

    int s = color.s + (direction * CONFIG_ZMK_RGB_UNDERGLOW_SAT_STEP);
    if (s < 0) {
        s = 0;
    } else if (s > SAT_MAX) {
        s = SAT_MAX;
    }
    color.s = s;

    return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction) {
    struct zmk_led_hsb color = state.color;

    int b = color.b + (direction * CONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP);
    color.b = CLAMP(b, 0, BRT_MAX);

    return color;
}

int zmk_rgb_underglow_change_hue(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.color = zmk_rgb_underglow_calc_hue(direction);

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_sat(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.color = zmk_rgb_underglow_calc_sat(direction);

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_brt(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.color = zmk_rgb_underglow_calc_brt(direction);

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_spd(int direction) {
    if (!led_strip)
        return -ENODEV;

    if (state.animation_speed == 1 && direction < 0) {
        return 0;
    }

    state.animation_speed += direction;

    if (state.animation_speed > 5) {
        state.animation_speed = 5;
    }

    return zmk_rgb_underglow_save_state();
}

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) ||                                          \
    IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
struct rgb_underglow_sleep_state {
    bool is_awake;
    bool rgb_state_before_sleeping;
};

static int rgb_underglow_auto_state(bool target_wake_state) {
    static struct rgb_underglow_sleep_state sleep_state = {
        is_awake : true,
        rgb_state_before_sleeping : false
    };

    // wake up event while awake, or sleep event while sleeping -> no-op
    if (target_wake_state == sleep_state.is_awake) {
        return 0;
    }
    sleep_state.is_awake = target_wake_state;

    if (sleep_state.is_awake) {
        if (sleep_state.rgb_state_before_sleeping) {
            return zmk_rgb_underglow_on();
        } else {
            return zmk_rgb_underglow_off();
        }
    } else {
        sleep_state.rgb_state_before_sleeping = state.on;
        return zmk_rgb_underglow_off();
    }
}

static int rgb_underglow_event_listener(const zmk_event_t *eh) {

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
    if (as_zmk_activity_state_changed(eh)) {
        return rgb_underglow_auto_state(zmk_activity_get_state() == ZMK_ACTIVITY_ACTIVE);
    }
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
    if (as_zmk_usb_conn_state_changed(eh)) {
        return rgb_underglow_auto_state(zmk_usb_is_powered());
    }
#endif

    return -ENOTSUP;
}

ZMK_LISTENER(rgb_underglow, rgb_underglow_event_listener);
#endif // IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE) ||
       // IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE)
ZMK_SUBSCRIPTION(rgb_underglow, zmk_activity_state_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_USB)
ZMK_SUBSCRIPTION(rgb_underglow, zmk_usb_conn_state_changed);
#endif

SYS_INIT(zmk_rgb_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
