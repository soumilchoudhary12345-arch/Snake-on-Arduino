# Connections — Snake on Arduino

Full pin map for the three components: **Arduino Uno + JHD162A LCD (I2C) + HW-004 joystick**.

## At a glance

| Arduino pin | Goes to | Component pin | Direction | Used for |
|-------------|---------|---------------|-----------|----------|
| **A4** | LCD backpack | SDA | I2C data | LCD commands/text (via `Wire`) |
| **A5** | LCD backpack | SCL | I2C clock | LCD commands/text (via `Wire`) |
| **5V** | LCD backpack | VCC | power | LCD power + backlight |
| **GND** | LCD backpack | GND | power | LCD ground |
| **A0** | Joystick | VRx | input (analog) | Left/right steering (`analogRead`) |
| **A1** | Joystick | VRy | input (analog) | Up/down steering (`analogRead`) |
| **D2** | Joystick | SW | input (digital) | Stick button, `INPUT_PULLUP` (active LOW) |
| **5V** | Joystick | +5V | power | Joystick power (potentiometers) |
| **GND** | Joystick | GND | power | Joystick ground |
| **D13** | (onboard) | LED_BUILTIN | output | Fast-blink error if LCD not found |
| **A2** | — | *nothing* | floating | Deliberately unconnected: picks up noise for the RNG seed |

Pin budget: **A0, A1, A2, A4, A5, D2, D13** used out of the Uno's full set — nothing else is touched.

## LCD — JHD162A 16×2 with I2C backpack (PCF8574)

```
        JHD162A + backpack              Arduino Uno
       ┌──────────────────┐
  GND ─┤ GND              │────────── GND
  VCC ─┤ VCC              │────────── 5V
  SDA ─┤ SDA         A4 ──┤── SDA (I2C data)
  SCL ─┤ SCL         A5 ──┤── SCL (I2C clock)
       └──────────────────┘
```

- Only **4 wires** (GND, VCC, SDA, SCL) — a parallel LCD would need 6+ digital pins.
- I2C address: **0x27** or **0x3F** — both are scanned automatically in `beginLcd()`, so there is nothing to configure or solder.
- Small blue trimmer on the backpack = **contrast**. Jumper on the backpack = **backlight on/off**.
- If neither address answers: D13 blinks fast and the reason is printed on the Serial Monitor (115200).

## Joystick — HW-004 module (5 pins)

```
        HW-004                       Arduino Uno
       ┌────────────┐
  GND ─┤ GND   VRx ├─── A0   (X axis: 0..1023, ~512 centre)
   5V ─┤ +5V   VRy ├─── A1   (Y axis: 0..1023, ~512 centre)
        │    SW  ├─── D2   (stick press, closes to GND)
       └────────────┘
```

- **VRx/VRy** are potentiometer outputs → read with `analogRead()`; the centre is calibrated at boot (32 averaged samples), dead band = ±120 counts.
- **SW** is a momentary switch to GND → read on **D2** with `INPUT_PULLUP`, so no external resistor is needed; pressed = `LOW`, debounced 40 ms.
- Direction decode is edge-detected: one turn per tilt-to-leave-centre, no repeat while held.

## Not connected / free pins

| Pin | Status |
|-----|--------|
| A2 | Intentionally floating — its electrical noise seeds `random()` so the food pattern differs every reset |
| A3, D3–D12, D0/D1 (Serial), 3V3, RESET, VIN | Free for future additions |

## Uploading the code — how the sketch gets onto the board

The wiring is only half the story: the PC talks to the Uno over the **same USB cable**, both for flashing and for debugging.

```
   PC                                   Arduino Uno
┌──────────────────┐   USB (5V + serial)   ┌─────────────────────────┐
│ Arduino IDE      │───────────────────────│ ATmega16U2 USB-serial   │
│  .ino → .hex     │   writes flash via    │        ↕ UART           │
│  avrdude flasher │───────────────────────│ ATmega328P + bootloader │
│  Serial Monitor  │◀──── debug text ──────│ (game runs from here)   │
└──────────────────┘      115200 baud      └─────────────────────────┘
```

**The pipeline (one click of → Upload):**

1. **Compile** — Arduino IDE turns `snake_lcd.ino` (+ `Wire` and `LiquidCrystal_I2C` libraries) into a `.hex` binary with `avr-gcc`.
2. **Transfer** — `avrdude` sends it over USB through the Uno's onboard USB-serial chip; the small **bootloader** on the ATmega328P receives it and writes it to flash.
3. **Run** — the bootloader jumps into your sketch: `setup()` then `loop()` forever. After that **the PC can be unplugged** — the game runs standalone from USB power.

**What you need in the IDE:**

- **Board:** `Tools → Board → Arduino Uno` (or "Arduino Nano" for a clone)
- **Port:** `Tools → Port` — the USB device created by the cable:
  - `/dev/ttyACM0` — genuine Uno (ATmega16U2 interface)
  - `/dev/ttyUSB0` — CH340/CP2102 clones (this is also what `play.py` assumes)
  - Your user must be in the serial group (`uucp` on Arch) or the port will refuse to open
- **Library:** `Tools → Manage Libraries` → install **"LiquidCrystal I2C" by Frank de Brabander**
- Then press **→ Upload** (verify + flash happens in one step)

**After upload — two ways to use the same port:**

| Use | How |
|-----|-----|
| Watch debug output | IDE `Tools → Serial Monitor`, baud **115200** — prints the found I2C address, "Entry page…", GAME OVER reasons, or the LCD-not-found error |
| Play/debug from the PC | `python3 play.py` (needs `pyserial`) — same 115200 baud, forwards arrow keys to the board, echoes sketch output back |

Important: **the code lives on the Arduino, not on the PC.** Re-upload only when the sketch changes; power-cycling the Uno re-runs whatever was last flashed, and unplugging USB mid-game is harmless — the modules are powered by the same 5 V line.

## Checklist

1. LCD: GND, VCC, SDA→A4, SCL→A5 (4 wires)
2. Joystick: GND, +5V, VRx→A0, VRy→A1, SW→D2 (5 wires)
3. Connect the Uno to the PC by USB (board + port selected in Arduino IDE, `LiquidCrystal_I2C` library installed)
4. Click **→ Upload** on `snake_lcd.ino` — the game is now stored on the board itself
5. Title page appears → tilt the stick to play (PC can be unplugged)
