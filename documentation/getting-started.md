# Getting started

## 1. Build the hardware

You need three things:

- a HUB75 / HUB75E LED matrix, 64x64 (a 64x32 panel works with the Tetris face only)
- an ESP32 board
- a 5 V power supply of 3 A or more for the panel

Wire the panel to the ESP32 the way the [ESP32-HUB75-MatrixPanel-I2S-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-I2S-DMA#2-wiring-esp32-with-the-led-matrix-panel) library describes. The default connections are in this diagram (from upstream Clockwise):

[![Wiring: ESP32 to HUB75 panel](../docs/images/display_esp32_wiring_thumb.png)](../docs/images/display_esp32_wiring_bb.png)

Ready-made alternatives are listed in the [main README](../README.md#what-you-need). The `3d-files/RGB-Matrix-P3-64x64` folder holds printable frame parts for a 64x64 P3 panel (outer frame variants with a cable or SD-card hole, inner frame, fasteners, joints and a mesh).

Power the panel from its own supply, not from the ESP32's USB port.

## 2. Flash the firmware

### From the browser (easiest)

1. Open <https://vandalon.github.io/clockwizer/> in **Chrome or Edge on a computer** (Firefox and Safari have no WebSerial).
2. Connect the ESP32 with a USB cable that carries data (some charging cables don't).
3. Pick a clockface and press **Connect and install**. Choose the ESP32's serial port in the dialog.
4. Wait until it is done. The installer then offers to connect the clock to WiFi, see below.

The installer writes the complete flash: bootloader, partition table, boot data and the clockface. That makes it the right tool for a blank ESP32, and for moving a clock that ran another Clockwise firmware to this one.

If the serial port doesn't show up, install the USB driver for your board's USB-to-serial chip (CP210x or CH340) and try another cable.

### From source

See [development.md](development.md#building-and-flashing).

## 3. Connect to WiFi

The clock needs a 2.4 GHz network; 5 GHz networks don't work.

**While flashing:** the browser installer asks for the network name and password right after the install (it speaks the Improv WiFi protocol with the clock over USB).

**Without USB:** on a clock with no saved network (or after *WiFi setup* on the settings page) it opens its own network called `Clockwise-Wifi` and shows a QR code. Scan it, or join that network by hand, choose your WiFi in the page that opens and enter the password. The setup page closes after five minutes.

If the clock can't connect it shows a message on the display and falls back to the setup network.

## 4. Open the settings page

For a few seconds after each start, and when you want it, the clock shows a QR code. Scanning it opens the settings page. You can also use:

- `http://clockwise.local` (works on most phones and computers; needs mDNS)
- the clock's IP address, which your router lists, or which the clock prints on its serial log
- the list *Other clocks on your network* on the settings page of another clock

Choose your timezone on the page. The clock sets its time over NTP, so it needs internet access.

Everything else is explained in [web-ui.md](web-ui.md).

## 5. Optional: a light sensor

Connect a light-dependent resistor (LDR) to an ESP32 analog pin to let the display dim when the room is dark. The default pin is GPIO 35. If you use another pin, set it as described in [control-api.md](control-api.md#advanced-settings). Then switch on *Auto brightness* in the settings, and use the live *Light in the room now* value to set where dark and bright are.
