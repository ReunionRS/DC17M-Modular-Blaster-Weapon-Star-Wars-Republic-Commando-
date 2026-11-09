# DC-17M — Electronics and Wiring Documentation

> Documentation for an Arduino Nano-based DC-17M cosplay prop electronics project.  
> **Wiring diagram:** [Open the Cirkit Designer project](https://app.cirkitdesigner.com/project/09446d83-6643-4f1f-982b-d6ccb8da8b40)
> 🇷🇺 [Читать документацию на русском](README_RU.md)

## Contents

- [Overview](#overview)
- [Components](#components)
- [Arduino Nano Pinout](#arduino-nano-pinout)
- [Module Connections](#module-connections)
- [Power Supply](#power-supply)
- [Operating Logic](#operating-logic)
- [DFPlayer Mini Audio Files](#dfplayer-mini-audio-files)
- [Testing and Troubleshooting](#testing-and-troubleshooting)
- [Assembly Notes](#assembly-notes)

## Overview

The project combines electronic effects and controls for a DC-17M cosplay prop:

- three modes: **ASSAULT**, **SNIPER**, and **GRENADE**;
- simulated firing effects using WS2812 addressable LEDs;
- ammunition display using a four-digit TM1637 module;
- a removable magazine detected by a Hall sensor;
- sound effects played by a DFPlayer Mini;
- an SPI TFT display for the Anti-armor module;
- a separate grenade-presence sensor;
- a servo-operated grenade-module mechanism.

This document describes the intended functions and the pin assignments used by the project. Before soldering, compare every connection with the current Cirkit Designer diagram and the silkscreen on the actual modules. Pin layouts may differ between manufacturers.

## Components

The Cirkit Designer drawing includes the following main modules:

| Component | Purpose |
|---|---|
| Arduino Nano | Main controller |
| DFPlayer Mini | Audio playback from microSD |
| Speaker and audio module | Sound output |
| TM1637, 4-digit | Ammunition display |
| Main WS2812 strip | Muzzle-flash effect |
| Magazine WS2812 strip | Magazine status indicator |
| Magazine Hall sensor | Detects magazine insertion/removal |
| Module Hall sensor | Additional magnetic module detection |
| Grenade Hall sensor | Detects a grenade in the module |
| Servo motor | Moves the grenade-module mechanism |
| 0.96-inch 80×160 SPI IPS TFT | Anti-armor module display |
| FIRE button | Trigger input |
| MODE button | Mode selection |
| Power switch | Main power control |

The Cirkit Designer image also shows two 18650 Li-ion cells and a power section. Verify the exact battery configuration and power-conversion hardware against the physical build before using that part of the diagram.

## Arduino Nano Pinout

The following table is based on the project configuration and the labels visible in the wiring diagram.

| Arduino Nano pin | Connection | Function |
|---|---|---|
| D2 | FIRE button | Trigger input, `INPUT_PULLUP` |
| D3 | Magazine LED DIN | Magazine WS2812 data |
| D4 | Module Hall sensor OUT | Additional Hall sensor |
| D5 | MODE button | Mode selection, `INPUT_PULLUP` |
| D6 | Grenade Hall sensor OUT | Grenade-presence detection |
| D7 | TFT RES/RST | TFT reset |
| D8 | TFT DC | TFT data/command selection |
| D9 | Servo signal | Servo control |
| D10 | TFT CS | TFT SPI chip select |
| D11 | TFT SDA/MOSI | SPI data output (MOSI) |
| D13 | TFT SCL/SCK | SPI clock |
| A1 | DFPlayer TX → Arduino RX | SoftwareSerial receive |
| A2 | Arduino TX → DFPlayer RX | SoftwareSerial transmit |
| A3 | Main LED DIN | Main WS2812 data |
| A4 | TM1637 DIO | TM1637 data |
| A5 | TM1637 CLK | TM1637 clock |
| A6 | Magazine Hall sensor OUT | Analog Hall sensor input |

**DFPlayer Mini note:** the firmware defines `MP3_RX = A1` and `MP3_TX = A2`. The serial connection is crossed: DFPlayer TX → Arduino A1, and Arduino A2 → DFPlayer RX. If the DFPlayer RX input requires signal attenuation, add the series resistor or voltage-divider recommended for your module.

**SPI note:** on a classic Arduino Nano, the hardware SPI pins are D11 (MOSI) and D13 (SCK). Some TFT boards label these pins `SDA` and `SCL`; in this SPI connection they are not I²C SDA/SCL.

## Module Connections

### 1. FIRE and MODE buttons

Connect each button between its assigned digital input and GND:

- FIRE → D2;
- MODE → D5.

The firmware uses `INPUT_PULLUP`: a released button reads `HIGH`, and a pressed button reads `LOW`. Do not wire these buttons between the input and +5V with this configuration.

### 2. WS2812 LED strips

**Main firing-effect strip:**
- DIN → A3;
- +5V → regulated +5V rail;
- GND → common ground.

**Magazine indicator strip:**
- DIN → D3;
- +5V → regulated +5V rail;
- GND → common ground.

Follow the arrows printed on the strip: data must enter through `DIN`, not `DOUT`. Both strips must share ground with the Arduino. A 330–470 Ω series resistor in each data line and an approximately 500–1000 µF electrolytic capacitor across +5V/GND near the strip power input are recommended. Do not route LED-strip current through the Nano's power traces.

### 3. TM1637 display

- DIO → A4;
- CLK → A5;
- VCC → +5V, if supported by the exact display module;
- GND → GND.

The display shows the current ammunition count for the selected mode.

### 4. Hall sensors

| Sensor | Signal pin | Purpose |
|---|---|---|
| Magazine Hall sensor | A6 | Detects magazine removal/insertion |
| Additional module Hall sensor | D4 | Separate magnetic module detection |
| Grenade Hall sensor | D6 | Detects grenade presence |

Connect sensor power to the voltage supported by the specific module and connect GND to common ground. Check the `S`, `+`, and `−` markings on the actual board. Hall modules may have active-LOW or active-HIGH outputs; verify the polarity and match it in the firmware. The magazine sensor on A6 is read as an analog input in the described configuration; D4 and D6 are digital inputs if that is how the current firmware is implemented.

### 5. DFPlayer Mini and speaker

- DFPlayer TX → Arduino A1;
- DFPlayer RX ← Arduino A2;
- VCC → supply voltage supported by the module;
- GND → common ground;
- speaker → DFPlayer SPK1/SPK2 outputs, when driving a speaker directly.

The microSD card must be compatible with the module and contain audio files in the expected folder structure. Do not connect SPK1/SPK2 to GND or to the line input of another amplifier: these are bridged speaker outputs.

### 6. Anti-armor SPI TFT (0.96-inch, 80×160)

The project configuration assigns:

- RES/RST → D7;
- DC → D8;
- CS → D10;
- SDA/MOSI → D11;
- SCL/SCK → D13;
- VCC → supply supported by the display board;
- GND → common ground;
- BLK/BL → backlight supply according to the board markings.

TFT boards may use different controllers and pinouts. Confirm whether your exact board accepts 5 V on VCC and its logic inputs. If it is a 3.3 V-only display, use the correct supply and logic-level conversion.

### 7. Servo motor

- signal wire → D9;
- power → a suitable supply rail;
- GND → common ground with the Arduino.

Do not power the servo through the Arduino Nano's 5V pin. A servo can draw a large transient current when starting or stalled, which may reset the controller. The supply must support the servo's peak current.

## Power Supply

The Cirkit Designer drawing shows two 18650 Li-ion cells. If connected in series, a fully charged pair can reach approximately 8.4 V. **Never connect this voltage directly to the Arduino 5V rail, WS2812 strips, DFPlayer, or TFT display.**

A series Li-ion pair requires:
- a suitable 2S protection/balancing board (BMS with balancing);
- a charger designed for a 2S Li-ion pack;
- a DC-DC converter providing regulated 5 V and sufficient continuous/peak current;
- secure battery mounting and insulation.

For the current USB power-bank setup, use the bank's regulated 5 V output. A 5 V / 2 A power bank may work under moderate load, but current headroom is limited when the servo moves, audio is loud, and LEDs are on at the same time. If the Nano resets, displays flicker, or audio glitches, measure the supply under load and consider a higher-current source.

Recommended power topology:
1. Source +5 V → main power switch.
2. The switched +5 V rail branches separately to the Arduino and peripheral modules.
3. The servo and WS2812 strips are powered from the power rail, not through the Nano's traces or I/O pins.
4. All grounds are connected to a common ground.
5. Do not connect two independent 5 V sources to the same rail at once (for example, a power bank and a computer USB port) unless the circuit includes proper power isolation.

Place the power switch in series with the source's positive wire. Its DC voltage and current rating must be suitable for the maximum system load; choose an adequate current margin and check the DC rating, not only the AC rating.

## Operating Logic

### ASSAULT

- Total ammunition capacity: 300 shots.
- One magazine: 60 shots.
- After 60 shots, the magazine must be removed and reinserted.
- Once the sensor confirms a magazine change, the magazine is replenished to 60 rounds and the total reserve is reduced.
- When the counter reaches `0000`, reinserting the magazine starts a new `0300` cycle, if this behavior is enabled in the firmware version being used.
- Firing is locked while the magazine sensor reports that the magazine is removed.

### SNIPER

- 5 rounds in the magazine;
- 15 rounds in reserve;
- single-shot operation;
- no automatic fire.

### GRENADE

- Up to 4 grenades;
- single-shot operation;
- the grenade sensor detects insertion/replacement;
- the servo operates the module's mechanical element according to the firmware logic.

The exact reload rules, ammunition reset behavior, and servo sequence depend on the `.ino` firmware currently installed. This document describes the intended project behavior; update this section whenever the firmware changes.

## DFPlayer Mini Audio Files

The firmware uses the following track numbers:

| Track | Sound name | Event |
|---:|---|---|
| 0001 | `blasterreload` | Assault reload |
| 0002 | `blastershot` | Assault firing |
| 0003 | `grenadeshoot` | Grenade launcher firing |
| 0004 | `noammo` | Empty magazine / firing blocked |
| 0005 | `sniperreload` | Sniper reload |
| 0006 | `snipershoot` | Sniper firing |
| 0007 | `switch` | Mode switch |

With `playMp3Folder()`, the DFPlayer Mini library commonly expects an `MP3` folder on the microSD card with numbered files such as `0001.mp3`, `0002.mp3`, and so on. Check the requirements of the exact library and card format. The names in the table are descriptive labels; the numbered filenames are what matter on the card.

## Testing and Troubleshooting

Test each subsystem separately before powering everything at once:

1. **Power:** measure the voltage between +5V and GND with no load and while the servo/LEDs are operating.
2. **Arduino:** confirm that the board boots and the Serial Monitor prints its ready message.
3. **Buttons:** test D2 and D5; pressing a button should change its input from HIGH to LOW.
4. **WS2812:** test each strip separately and confirm that data enters DIN.
5. **TM1637:** display a test number.
6. **Hall sensors:** monitor raw A6 readings and D4/D6 logic levels while moving the magnet. Confirm active polarity.
7. **DFPlayer:** run a standalone player test first and confirm that the module responds and plays all seven tracks.
8. **TFT:** verify the controller model, SPI wiring, and library configuration.
9. **Servo:** test it separately without mechanical load, then check current draw and supply stability.

If the Nano resets when the servo moves, do not try to solve it with software delays. Check the power supply, common ground, and available current first.

## Assembly Notes

- Disconnect power before soldering or moving wires.
- Do not rely on wire colors alone; check the pin labels.
- Do not connect an unknown sensor output to the Arduino before checking its voltage.
- Prevent battery short circuits and insulate exposed terminals.
- Do not charge Li-ion cells using a DIY circuit without a suitable charger and protection.
- Before installing electronics in the prop body, insulate the boards, provide strain relief for wires, and keep the power switch accessible.

## Interactive Wiring Diagram

[DC-17M — Cirkit Designer project](https://app.cirkitdesigner.com/project/09446d83-6643-4f1f-982b-d6ccb8da8b40)

The Cirkit Designer project is the primary visual reference for the wiring. If it differs from the pinout table or the physical build, verify the actual wiring and update this documentation.
