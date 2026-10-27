# Snake on Arduino

A complete Snake game running on an **Arduino Uno**, shown on a **16x2 LCD (I2C)** and controlled **only with an HW-004 joystick module**. No PC needed at play time — power it, tilt the stick, play.

```
  ┌────────────────┐        ┌──────────────┐
  │  █████  SNAKE  │        │   Arduino    │
  │ push joystick! │        │    Uno       │
  └────────────────┘        └──────┬───────┘
   JHD162A 16x2 LCD               │ A4/A5 → LCD (I2C)
   (title screen)                 │ A0/A1 → VRx/VRy, D2 → SW
                                  └──── HW-004 joystick
```

---

## The Setup — three components

| # | Component | Why this one |
|---|-----------|--------------|
| 1 | **Arduino Uno** (or compatible) | 2 KB RAM is enough for a 64-cell board; analog pins read the stick directly; no extra modules needed |
| 2 | **JHD162A 16x2 LCD + 4-pin I2C backpack (PCF8574)** | Parallel LCDs eat 6+ digital pins; I2C needs only **A4 (SDA) + A5 (SCL)**. Address `0x27`/`0x3F` is auto-scanned at boot, so there is nothing to configure |
| 3 | **HW-004 joystick module** (GND, +5V, VRx, VRy, SW) | Two analog pots give 4-way steering on **A0/A1**, the stick button uses **D2** with the internal pull-up (no external resistor). 3 pins total for full control |

### Wiring

```
LCD (I2C)                      Joystick (HW-004)
  GND → GND                      GND → GND
  VCC → 5V                       +5V → 5V
  SDA → A4                       VRx → A0
  SCL → A5                       VRy → A1
                                 SW  → D2  (INPUT_PULLUP, closes to GND)
```

---

## The Story — how three people built it

The project was split into three clean sections: **hardware/display**, **game rules**, and **input/tooling**. Each person owned one section end-to-end: they picked the parts, justified the choice, wrote their functions, and knew exactly which part of the file was theirs.

### Person 1 — Display & Hardware Engineer (the LCD)

**Ideation.** A 16x2 character LCD has no pixel graphics — only 32 character cells, which is too coarse for Snake. Their idea: **split every character cell in half** (top 4 pixel rows = one playfield row, bottom 4 rows = the next), turning the screen into a **16 × 4 = 64-cell board**. The snake is drawn as thin 3-pixel bars (2 px gap → body reads as one connected line); food is a fat full-width bar that blinks, so target and body can never be confused.

**Components & why.** Chose the **I2C JHD162A** over a parallel LCD to save pins (2 vs 6+), with an automatic `0x27`/`0x3F` address scan so different backpack clones just work. Failure is obvious without a Serial Monitor: the built-in LED (D13) fast-blinks if nothing answers on the bus.

**What they implemented** (in `snake_lcd.ino`):

| Function | Job |
|----------|-----|
| `beginLcd()` | Scan I2C addresses, create the `LiquidCrystal_I2C` object, upload the 8 custom glyphs |
| `lcdErrorBlink()` | D13 error blink when the LCD is missing/wired wrong |
| `buildCellGlyphs()`, `fillGlyph()` | Build the 8 custom characters (thin snake bars, fat food bars, mixed cells, title square) |
| `glyphFor(upper, lower)` | Pick which glyph renders a board cell from its contents |
| `drawBoard()` | Full 2-row redraw — unchanged cells get the same character again, so no flicker |
| `showEntry()`, `drawEntryHint()` | Title page: "SNAKE" + blinking "push joystick!" |
| `gameOver()`, `drawGameOverHint()` | GAME OVER page, bottom row alternates score / restart hint |
| `enterScoreFlash()`, `endScoreFlash()` | Score overlay on the top row after eating, then back to play |
| `printScoreLine()`, `scoreLineStart()` | Centered "Score: n" line for 1–3 digits |

### Person 2 — Game Rules & Logic Engineer (the brain)

**Ideation.** The rules were designed to fit the tiny screen: the snake **phases through the edges** (wrap-around board → walls never kill you, so all 64 cells stay playable), starts **4 blocks long**, **grows +1 per food**, and the game only ends on **self-collision**. Four game states keep the UI simple: `ENTRY → PLAYING → FLASH → GAMEOVER → ENTRY`. Food blinks (450 ms) to stand out; a short score flash rewards each bite without stopping the game for long.

**Components & why.** No extra hardware — but a hard constraint: **the Uno has 2 KB RAM**, so everything is fixed-size static data (`snake[64]` array, no `String`, no `malloc`). Directions use a compact index + delta tables (`DIR_DX`, `DIR_DY`), where "opposite direction" is just `dir ^ 1`.

**What they implemented** (in `snake_lcd.ino`):

| Function / data | Job |
|-----------------|-----|
| `GameState` enum + state globals | The 4-state machine driving the whole sketch |
| `startGame()` | Reset score/length/direction, seed RNG, place the 4-block snake, spawn food |
| `setDirection(d)` | Queue joystick turns; **180° reversals are ignored** (you can't eat yourself by flipping) |
| `moveSnake()` | Advance head, wrap at edges, detect food (grow + score + respawn) or self-hit (game over) |
| `spawnFood()` | Fast path: 64 random tries; fallback: full scan for a free cell |
| `kindOf()`, `visibleKind()` | What's in a cell (food/snake/empty), respecting the food blink phase |
| `cellOccupied()`, `colOf()`, `rowOf()` | Board-index helpers |
| `loop()` state machine | Tick the snake every 200 ms, blink food/hints only while that state is active |
| Tuning constants | `MOVE_INTERVAL_MS`, `START_LENGTH`, `FOOD_ATTEMPTS`, `BLINK_MS`, … |

### Person 3 — Controls & Tooling Engineer (the joystick)

**Ideation.** The game had to be playable **without a PC** (it started with serial arrow keys). The HW-004 stick gives physical control: tilt to steer, press to start/restart. Raw analog sticks are noisy, so their key idea was **edge detection**: a tilt fires **once** when the stick leaves the dead zone and doesn't fire again until it returns to centre — a held stick can never spam turns.

**Components & why.** **HW-004** chosen because it needs only 3 pins: `VRx→A0`, `VRy→A1`, `SW→D2`. The button uses `INPUT_PULLUP` (active LOW to GND → no external resistor). Two analog reads beat a D-pad of 4 digital pins and gives analog dead-zone control for free.

**What they implemented** (in `snake_lcd.ino` + `play.py`):

| Function | Job |
|----------|-----|
| `readJoystickDir()` | Calibrated-centre decode with a **120-count dead band**, a **sticky dominant axis** (+60 margin) so ADC noise near a diagonal can't zigzag the snake |
| `pollJoystick(now)` | Edge-detects tilt changes; debounce (40 ms) on the button, fires only on press |
| `onJoystickDir(d)` | Routes a tilt by state: steer / start / dismiss score / return to title |
| `onJoystickButton()` | Same routing for the stick button (SW) |
| Centre calibration in `setup()` | Averages 32 samples per axis at boot so an off-centre pot doesn't create a phantom tilt |
| RNG seeding (setup) | Mixes `micros()` with noise from floating pin **A2** → a different food pattern every reset |
| **`play.py`** | Bonus tool from the earlier serial-control era: a Python terminal bridge (`pyserial`) that lets you play/debug over USB with arrow keys — kept for development |

---

## Files in this repository

| File | What it is |
|------|------------|
| `snake_lcd.ino` | The entire game — one self-contained sketch. Sections in order: LCD setup → joystick → game tuning → glyph/render layer → screens → game logic → input handlers → `setup()`/`loop()` |
| `play.py` | Optional PC-side helper: plays/debugs the board over USB serial (arrow keys → sketch), `python3 play.py [/dev/ttyUSB0]` |
| `README.md` | This file |

## How to run

1. Wire the LCD and joystick as shown above.
2. In Arduino IDE, install library **"LiquidCrystal I2C" by Frank de Brabander** (Library Manager).
3. Open `snake_lcd.ino`, select your board/port, upload.
4. The title page appears — **tilt the joystick (or press the stick)** to start.
5. Steer with the stick; the game ends only when the snake hits itself. After GAME OVER, tilt/press again for a new round.

## Game rules

- Board: **16 × 4 half-cells (64 playable cells)**, wrap-around edges — no wall deaths.
- Snake starts **4 blocks** long, **+1 block and +1 point** per food.
- Food blinks; eat it to trigger a short score flash.
- Only **self-collision** ends the game.
