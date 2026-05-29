# Zephyr™ Mechanical Keyboard (ZMK) Firmware

[![Discord](https://img.shields.io/discord/719497620560543766)](https://zmk.dev/community/discord/invite)
[![Build](https://github.com/zmkfirmware/zmk/workflows/Build/badge.svg)](https://github.com/zmkfirmware/zmk/actions)
[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-v2.0%20adopted-ff69b4.svg)](CODE_OF_CONDUCT.md)

## Glove 80 Dongle Overview

> **Inspired by:** [hopg's ZMK repository](https://github.com/hopg/zmk/tree/slice-mk-glove80-rh-rgb) and [darknao's Per key/layer RGB underglow PR](https://github.com/moergo-sc/zmk/pull/36)

The topology change allows both Glove80 halves to work as Bluetooth peripherals with the dongle acting as the central device. It is for a battery efficiency-first setup where the Glove80 is deskbound and you don't want to be recharging the left half every few weeks. In my setup, the dongle is permanently plugged into a KVM and so dongle doesn't need to be moved.

**⚠️ WARNING:** This is not an official release from MoErgo. Proceed with caution. If the steps are not followed correctly you may damage your dongle and/or your Glove80. This setup has been tested and developed using the [MakerDiary nRF52840 MDK USB Dongle](https://wiki.makerdiary.com/nrf52840-mdk-usb-dongle/) since it is shipped with UF2 Bootloader. **This configuration has not been tested with other nRF52840 dongles**, so compatibility cannot be guaranteed.

### Features and Limitations

- Charge both halves of the Glove80 once every 4 months (often lasting even longer)
- RGB underglow rendering occurs only on peripheral devices; the dongle central does not render LEDs directly (indicators are only visible on connected Glove80 halves)
- Each half displays its own battery level on its own underglow LEDs (i.e. the left no longer displays the right half)
  - The bottom row will display the battery detail offset within the current 20%.
    - Each LED representing 4% increments with each increment represented by different colours (lilac - magenta - yellow - green)
    - For example, with the first row showing 5 greens and the second row showing 1 yellow, then the battery level is 83%. If the first row shows 4 green and the second row shows 3 greens and a magenta then the battery level is 74%
  - Remote peripheral battery fetching is intentionally avoided to minimize BLE traffic and power consumption
  - Conscious design decision: active BLE queries for cross-peripheral battery fetching contradict the battery-efficiency objective as it adds four extra BLE communications
  - The "Standard Edition" Glove80s in the Kickstarter/Backerkit campaigns do not have RGB LEDs on the right half. So, only the left half's battery level will be displayed
- RGB underglow indicators changes from the standard Glove80:
  - The USB output will show the status of the dongle's USB output (central)
  - Each peripheral shows its local USB enumeration status at T3
  - Each half displays its own local battery level with color-coded indicators
- Semantic underglow property naming for dongle-central topology:
  - New properties: `bat-local` (local device battery), `bat-left`, `bat-right` (Glove80-specific)
  - Deprecated properties: `bat-lhs`, `bat-rhs` (legacy from left-central era) maintained for backwards compatibility
  - Central USB state indicator (`central-usb` property) shows dongle's USB connection status on left half
- If you decide to use a different dongle, you will need to update the dtsi and overlays of the board/shield otherwise the HID (scroll, num, caps locks) indicators will only intermittently work
  - The issue you may see is that the HID indicators would regularly not show when reconnecting to USB (usually after initial firmware flash and/or when the peripherals are not turned on early enough after the dongle is plugged in)
  - Sort of a non-issue since toggling one of the HID locks will force the indicators to display properly (at least until the next time the dongle is power-cycled)
  - The fix is to add one of the dongle's unused but existing GPIO to the matrix so that the dongle registers with the host as an 'active' HID keyboard
- Still allows to pair over Bluetooth with four different hosts if you want
- KVM compatibility: I had issues with the dongle plugged into the dedicated keyboard/mouse USB ports (the emulated ports that don't have the USB passthrough):
  - The issue I was having was that when I was over a certain typing speed (not very fast, ~30 WPM), the KVM would miss keys or repeat keys
  - My workaround was to use the [Handheld Scientific Bluetooth Adapter BT-500](http://handheldsci.com/wp/wp-content/uploads/Manual_Full_v5.4.5.pdf) in between the KVM and the dongle, purely for the USB bridging mode
  - It looks like the BT-500 is not readily available anymore and they have replaced it with the [BT-600](http://handheldsci.com/kb/). **⚠️ WARNING:** I have not tested the BT-600 so I can't confirm if the behaviour will be the same
- I didn't use the GitHub build workflow so it most likely won't work. I have included two scripts that were used to setup the environment (install all the requirements) and build all the firmware files
- I was working in WSL so I copy the files to my Windows environment at the end (which you can remove if you don't want that to happen)
- This worked for me but it may not for you. Please update as you see fit

### Configuration and Build

Please see the west-based Custom ZMK Configuration for a Dongle version of the MoErgo Glove80. Please choose the branch you want to use:

- [Glove80 Dongle](https://github.com/phy-sandbox/glove80-zmk-config-west/tree/glove80_dongle)
- [Glove 80 Dongle with Per RGB/Layer Underglow](https://github.com/phy-sandbox/glove80-zmk-config-west/tree/integration/pr36-rgb-layer)

[ZMK Firmware](https://zmk.dev/) is an open source ([MIT](LICENSE)) keyboard firmware built on the [Zephyr™ Project](https://www.zephyrproject.org/) Real Time Operating System (RTOS). ZMK's goal is to provide a modern, wireless, and powerful firmware free of licensing issues.

Check out the website to learn more: https://zmk.dev/.

You can also come join our [ZMK Discord Server](https://zmk.dev/community/discord/invite).

To review features, check out the [feature overview](https://zmk.dev/docs/). ZMK is under active development, and new features are listed with the [enhancement label](https://github.com/zmkfirmware/zmk/issues?q=is%3Aissue+is%3Aopen+label%3Aenhancement) in GitHub. Please feel free to add 👍 to the issue description of any requests to upvote the feature.
