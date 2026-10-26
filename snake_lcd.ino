/* ==========================================================================
 *  SNAKE  -  Arduino Uno + JHD162A 16x2 LCD (I2C) + HW-004 joystick
 * ==========================================================================
 *
 *  A small, standalone Snake game on a JHD162A with a mounted I2C backpack
 *  (4 pins, only two Arduino pins needed).
 *
 *  The game is controlled ONLY with the HW-004 joystick module: push the
 *  stick to steer, press the stick button (SW) to start / restart.
 *
 *  --------------------------------------------------------------------------
 *  HARDWARE / WIRING
 *  --------------------------------------------------------------------------
 *    - Arduino Uno (or compatible)
 *    - JHD162A 16x2 LCD with 4-pin I2C backpack (PCF8574)
 *    - HW-004 joystick module (5 pins: GND, +5V, VRx, VRy, SW)
 *
 *    LCD pin | Name | Arduino          Joystick pin | Name | Arduino
 *    --------+------+--------          -------------+------+--------
 *      GND   | GND  | GND                GND        | GND  | GND
 *      VCC   | VCC  | 5V                 +5V        | 5V   | 5V
 *      SDA   | SDA  | A4                 VRx        | VRx  | A0
 *      SCL   | SCL  | A5                 VRy        | VRy  | A1
 *                                       SW         | SW   | D2
 *
 *    D2 is read with INPUT_PULLUP: SW closes to GND when the stick is
 *    pressed.  A0/A1 read the stick axes (~512 at centre, 0..1023 range).
 *
 *    The small blue trimmer on the backpack sets the contrast.
 *    The backpack jumper enables the backlight.
 *
 *    I2C address: normally 0x27, on some boards 0x3F.  Both are scanned
 *    automatically at startup, so no setting is required.  If neither
 *    answers, the built-in LED (D13) blinks fast and the reason is printed
 *    on the Serial Monitor - that means the backpack is not wired right.
 *
 *    A4/A5 are the only pins the LCD uses; the joystick takes A0, A1 and D2.
 *
 *  --------------------------------------------------------------------------
 *  HOW TO PLAY
 *  --------------------------------------------------------------------------
 *    1. Upload the sketch.
 *    2. The ENTRY PAGE ("SNAKE" + blinking "push stick") appears and stays
 *       there until you tilt the joystick in any direction - that starts
 *       the game.
 *    3. Tilt the stick up / down / left / right to steer the snake.
 *       Pressing the stick button (SW) also starts / restarts a round.
 *    4. After GAME OVER the same entry page appears again; tilt the stick
 *       (or press SW) there to play another round.
 *
 *    Libraries (Library Manager): "LiquidCrystal I2C" by Frank de Brabander.
 *
 *  --------------------------------------------------------------------------
 *  DISPLAY / BOARD LAYOUT
 *  --------------------------------------------------------------------------
 *    A 16x2 character LCD has no pixel graphics - only 32 character cells.
 *    To get a finer board, every character cell is split in HALF: the top
 *    four pixel rows show one playfield row, the bottom four pixel rows
 *    show the next one.  The board is therefore
 *
 *        16 columns x 4 half-rows  =  64 playable cells
 *
 *    The snake is drawn as thin 3-pixel bars - each cell leaves only a 2px
 *    gap, so the body reads as one slim connected line.  The food is a FAT
 *    full-width bar that blinks (on/off every 450 ms) - the shapes are
 *    different, so the target never gets confused with the body.
 *
 *    Eight custom characters are used: thin snake bars (upper/lower/both),
 *    fat food bars (upper/lower), the two mixed cells where food and snake
 *    share one character, and a full square for the title screen.
 *
 *  --------------------------------------------------------------------------
 *  RULES
 *  --------------------------------------------------------------------------
 *    * The snake PHASES through the edges: leaving the screen on one side
 *      it reappears on the opposite side (the board wraps around), so the
 *      walls never kill it.
 *    * The snake starts 4 blocks long and GROWS by one block for every food
 *      it eats.  Eating food also scores a point and moves the food.
 *    * The game only ends when the snake runs into ITSELF.
 * ========================================================================== */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ========================== LCD (I2C) ======================================
// Addresses tried at startup; the first one that answers wins.
const uint8_t LCD_ADDR_A = 0x27;
const uint8_t LCD_ADDR_B = 0x3F;

LiquidCrystal_I2C *lcd = nullptr;   // created in setup() once the address is known

const uint8_t LCD_COLS = 16;        // physical LCD size
const uint8_t LCD_ROWS = 2;

// ========================== JOYSTICK (HW-004) ==============================
// VRx on A0, VRy on A1: analog axes, ~512 at centre, 0..1023 full range.
// SW on D2 with INPUT_PULLUP: closes to GND when the stick is pressed.
const uint8_t JOY_VRX = A0;
const uint8_t JOY_VRY = A1;
const uint8_t JOY_SW  = 2;

// Values within JOY_DEADBAND of the calibrated centre count as centred.
// Pots rarely sit at exactly 512, so the centre is sampled at boot.
const int JOY_DEADBAND = 120;   // ~12% of full travel, snappy but jitter-safe
const int JOY_AXIS_MARGIN = 60; // hysteresis: other axis must beat sticky one by this much

const unsigned long JOY_BTN_DEBOUNCE_MS = 40; // SW debounce window

int joyCenterX = 512, joyCenterY = 512;  // sampled in setup()

// ========================== GAME TUNING ====================================
const uint8_t COLS = 16;                  // playfield columns (= LCD columns)
const uint8_t ROWS = 4;                   // playfield half-rows (2 LCD rows x 2)
const uint8_t BOARD_CELLS = COLS * ROWS;  // 64 playable cells

const unsigned long MOVE_INTERVAL_MS = 200; // ms per snake cell (lower = faster)
const unsigned long SCORE_FLASH_MS   = 350; // score shown after eating food
const unsigned long BLINK_MS         = 400; // entry-page hint blink period
const unsigned long FOOD_BLINK_MS    = 450; // food is hidden/shown this often

const uint8_t START_LENGTH  = 4;   // snake length: fixed, never changes
const uint8_t FOOD_ATTEMPTS = 64;  // random tries before scanning every cell

// ========================== DIRECTIONS =====================================
// The opposite of any direction is (dir ^ 1); the joystick direction code
// is the same index, so decoding a tilt gives the direction.
const uint8_t DIR_UP = 0, DIR_DOWN = 1, DIR_LEFT = 2, DIR_RIGHT = 3;
const int8_t  DIR_DX[4] = { 0, 0, -1, 1 };   // column delta per direction
const int8_t  DIR_DY[4] = { -1, 1, 0, 0 };   // half-row delta per direction

// ========================== GAME STATES ====================================
enum GameState : uint8_t {
  STATE_ENTRY,     // entry page: "SNAKE" + blinking hint, waits for a key
  STATE_PLAYING,   // normal gameplay
  STATE_FLASH,     // score overlay after eating (short pause)
  STATE_GAMEOVER   // self-collision -> "GAME OVER" + score
};

// ===================== CELL CONTENT / GLYPH SLOTS ==========================
const uint8_t KIND_EMPTY = 0, KIND_SNAKE = 1, KIND_FOOD = 2;

// Snake = thin 3px bars.  Food = fat full-width bars, so the target is
// obvious and never confusable with the body.  Slots 0..6 draw the board;
// slot 7 is a full square used on the title screen.
const uint8_t GLYPH_SNAKE_UPPER = 0; // thin bar in the upper half-row
const uint8_t GLYPH_SNAKE_LOWER = 1; // thin bar in the lower half-row
const uint8_t GLYPH_SNAKE_BOTH  = 2; // thin bar in both half-rows
const uint8_t GLYPH_FOOD_UPPER  = 3; // fat bar in the upper half-row
const uint8_t GLYPH_FOOD_LOWER  = 4; // fat bar in the lower half-row
const uint8_t GLYPH_MIX_SF      = 5; // snake upper + food lower
const uint8_t GLYPH_MIX_FS      = 6; // food upper + snake lower
const uint8_t GLYPH_FULL        = 7; // full square (title decoration)
const uint8_t GLYPH_NONE        = 0xFF; // draw a space instead

byte cellGlyphs[8][8];

const byte THIN = 0x0E;   // .###.  3px bar - only 2px gap between cells, reads connected
const byte FAT  = 0x1F;   // #####

void fillGlyph(byte *g, uint8_t from, uint8_t to, byte value) {
  for (uint8_t r = from; r < to; r++) g[r] = value;
}

void buildCellGlyphs() {
  for (uint8_t i = 0; i < 8; i++) fillGlyph(cellGlyphs[i], 0, 8, 0x00);

  fillGlyph(cellGlyphs[GLYPH_SNAKE_UPPER], 0, 4, THIN);
  fillGlyph(cellGlyphs[GLYPH_SNAKE_LOWER], 4, 8, THIN);
  fillGlyph(cellGlyphs[GLYPH_SNAKE_BOTH],  0, 4, THIN);
  fillGlyph(cellGlyphs[GLYPH_SNAKE_BOTH],  4, 8, THIN);

  fillGlyph(cellGlyphs[GLYPH_FOOD_UPPER], 0, 4, FAT);
  fillGlyph(cellGlyphs[GLYPH_FOOD_LOWER], 4, 8, FAT);

  fillGlyph(cellGlyphs[GLYPH_MIX_SF], 0, 4, THIN);
  fillGlyph(cellGlyphs[GLYPH_MIX_SF], 4, 8, FAT);
  fillGlyph(cellGlyphs[GLYPH_MIX_FS], 0, 4, FAT);
  fillGlyph(cellGlyphs[GLYPH_MIX_FS], 4, 8, THIN);

  fillGlyph(cellGlyphs[GLYPH_FULL], 0, 8, FAT);
}

// Which custom character renders a board cell, from the kinds above.
uint8_t glyphFor(uint8_t upper, uint8_t lower) {
  if (upper == KIND_SNAKE && lower == KIND_SNAKE) return GLYPH_SNAKE_BOTH;
  if (upper == KIND_SNAKE && lower == KIND_FOOD)  return GLYPH_MIX_SF;
  if (upper == KIND_FOOD  && lower == KIND_SNAKE) return GLYPH_MIX_FS;
  if (upper == KIND_SNAKE) return GLYPH_SNAKE_UPPER;
  if (upper == KIND_FOOD)  return GLYPH_FOOD_UPPER;
  if (lower == KIND_SNAKE) return GLYPH_SNAKE_LOWER;
  if (lower == KIND_FOOD)  return GLYPH_FOOD_LOWER;
  return GLYPH_NONE;
}

// ========================== GAME STATES DATA ================================
// (fixed-size static data only - no String, no malloc)
uint8_t snake[BOARD_CELLS];     // snake[0] = head, snake[snakeLen-1] = tail
uint8_t snakeLen = START_LENGTH;
uint8_t foodPos   = 0;          // board index of the food (0..63)
uint8_t score     = 0;
uint8_t dir       = DIR_RIGHT;  // direction the head currently moves
uint8_t pendingDir = DIR_RIGHT; // direction queued by the joystick
GameState state = STATE_ENTRY;

unsigned long stateStartMs = 0;  // when the current state began
unsigned long lastMoveMs   = 0;  // when the snake last moved
unsigned long blinkMs      = 0;  // when the entry-page hint toggled
bool blinkOn = true;

unsigned long foodBlinkMs = 0;   // when the food dot last toggled
bool foodVisible = true;         // food is shown on the "on" phase only

// ============================ HELPERS ======================================
uint8_t colOf(uint8_t idx) { return idx % COLS; }  // board index -> column
uint8_t rowOf(uint8_t idx) { return idx / COLS; }  // board index -> half-row

// What sits in a board cell: food, snake (incl. head) or nothing.
uint8_t kindOf(uint8_t idx) {
  if (idx == foodPos) return KIND_FOOD;
  for (uint8_t i = 0; i < snakeLen; i++) {
    if (snake[i] == idx) return KIND_SNAKE;
  }
  return KIND_EMPTY;
}

// True if any snake segment occupies this board cell.
bool cellOccupied(uint8_t pos) {
  for (uint8_t i = 0; i < snakeLen; i++) {
    if (snake[i] == pos) return true;
  }
  return false;
}

// Place food on a free cell.  Random tries first, then a full scan.
void spawnFood() {
  for (uint8_t t = 0; t < FOOD_ATTEMPTS; t++) {
    uint8_t pos = (uint8_t)random(BOARD_CELLS);
    if (!cellOccupied(pos)) {
      foodPos = pos;
      foodVisible = true;
      foodBlinkMs = millis();
      return;
    }
  }
  for (uint8_t pos = 0; pos < BOARD_CELLS; pos++) {
    if (!cellOccupied(pos)) {
      foodPos = pos;
      foodVisible = true;
      foodBlinkMs = millis();
      return;
    }
  }
}

// What is actually drawn in a board cell.  The food counts as empty on the
// "off" phase of its blink - the fat food bar blinks, the snake stays put.
uint8_t visibleKind(uint8_t idx) {
  uint8_t k = kindOf(idx);
  if (k == KIND_FOOD && !foodVisible) return KIND_EMPTY;
  return k;
}

// Full redraw of both LCD rows.  Every character shows two playfield rows.
// Unchanged cells get the same character again, so there is no flicker.
void drawBoard() {
  for (uint8_t lcdRow = 0; lcdRow < LCD_ROWS; lcdRow++) {
    lcd->setCursor(0, lcdRow);
    uint8_t upperBase = (lcdRow * 2) * COLS;      // first cell of the upper row
    for (uint8_t col = 0; col < COLS; col++) {
      uint8_t upperIdx = upperBase + col;
      uint8_t lowerIdx = upperIdx + COLS;

      uint8_t g = glyphFor(visibleKind(upperIdx), visibleKind(lowerIdx));
      if (g == GLYPH_NONE) lcd->write(' ');
      else lcd->write(byte(g));
    }
  }
}

// ============================ SCREENS ======================================
// ---------------- ENTRY PAGE (shown at boot and after every game) ----------
void drawEntryHint() {
  lcd->setCursor(0, 1);
  if (blinkOn) {
    lcd->print(F("push joystick!"));       // 14 chars -> cols 0..13
  } else {
    for (uint8_t i = 0; i < LCD_COLS; i++) lcd->write(' ');
  }
}

void showEntry() {
  state = STATE_ENTRY;
  lcd->clear();
  lcd->setCursor(2, 0);
  lcd->write(byte(GLYPH_FULL));
  lcd->setCursor(5, 0);
  lcd->print(F("SNAKE"));
  lcd->setCursor(13, 0);
  lcd->write(byte(GLYPH_FULL));
  blinkOn = true;
  blinkMs = millis();
  drawEntryHint();
  Serial.println(F("Entry page - push the joystick to start"));
}

// Where a "Score: n" line is centred (1, 2 or 3 digits).
uint8_t scoreLineStart() {
  uint8_t len = 8;                              // "Score: 0"
  if (score >= 10)  len = 9;                    // "Score: 42"
  if (score >= 100) len = 10;                   // "Score: 123"
  return (LCD_COLS - len) / 2;
}

void printScoreLine() {
  lcd->setCursor(scoreLineStart(), 1);
  lcd->print(F("Score: "));
  lcd->print(score);
}

void gameOver() {
  state = STATE_GAMEOVER;
  lcd->clear();
  lcd->setCursor(3, 0);                 // "GAME OVER" = 9 chars -> col 3
  lcd->print(F("GAME OVER"));
  blinkOn = true;
  blinkMs = millis();
  drawGameOverHint();
  Serial.println(F("GAME OVER (hit itself) - push the joystick for the title"));
}

// Bottom row alternates score / "push joystick" so restarting is obvious.
void drawGameOverHint() {
  lcd->setCursor(0, 1);
  for (uint8_t i = 0; i < LCD_COLS; i++) lcd->write(' ');
  if (blinkOn) {
    lcd->setCursor(0, 1);
    lcd->print(F("push joystick!"));    // 14 chars -> cols 0..13
  } else {
    printScoreLine();
  }
}

// Score overlay on the TOP row only: the bottom row still shows the board.
void enterScoreFlash() {
  state = STATE_FLASH;
  stateStartMs = millis();
  drawBoard();               // refresh BOTH rows first - the move that ate
                             // the food has not been drawn yet, so the lower
                             // LCD row would otherwise stay one tick stale
  lcd->setCursor(0, 0);
  for (uint8_t i = 0; i < LCD_COLS; i++) lcd->write(' ');
  lcd->setCursor(scoreLineStart(), 0);
  lcd->print(F("Score: "));
  lcd->print(score);
}

void endScoreFlash() {
  state = STATE_PLAYING;
  lastMoveMs = millis();   // restart the movement timer -> no instant jump
  drawBoard();
}

// ============================ GAME LOGIC ===================================
void startGame() {
  score    = 0;
  snakeLen = START_LENGTH;
  dir = pendingDir = DIR_RIGHT;

  // every round gets a different food pattern (the key press time differs)
  randomSeed(micros() ^ (unsigned long)analogRead(A0));

  // Head on half-row 0, column 5; 4 blocks trailing to the left
  snake[0] = 5;
  snake[1] = 4;
  snake[2] = 3;
  snake[3] = 2;

  state = STATE_PLAYING;
  lastMoveMs = millis();

  spawnFood();
  drawBoard();
}

// Queue a new direction; 180-degree reversals are ignored.
void setDirection(uint8_t d) {
  if (d == dir) return;        // already heading that way
  if (d == (dir ^ 1)) return;  // opposite of current -> reversal, ignore
  pendingDir = d;
}

void moveSnake() {
  dir = pendingDir;  // apply the queued input at most once per tick

  int8_t newCol = (int8_t)colOf(snake[0]) + DIR_DX[dir];
  int8_t newRow = (int8_t)rowOf(snake[0]) + DIR_DY[dir];

  // The snake phases through the screen: it leaves on one edge and comes
  // back on the opposite one, so there are no walls to die against.
  if (newCol < 0)          newCol = COLS - 1;
  else if (newCol >= COLS) newCol = 0;
  if (newRow < 0)          newRow = ROWS - 1;
  else if (newRow >= ROWS) newRow = 0;

  uint8_t newHead = (uint8_t)(newRow * COLS + newCol);
  bool eating = (newHead == foodPos);

  if (eating) {
    // Grow: the head moves into the food cell and the tail stays put.
    uint8_t oldTail = snake[snakeLen - 1];
    for (uint8_t i = (uint8_t)(snakeLen - 1); i > 0; i--) {
      snake[i] = snake[i - 1];
    }
    snake[0] = newHead;
    snake[snakeLen] = oldTail;   // keeps the old tail -> one cell longer
    snakeLen++;

    if (score < 255) score++;
    spawnFood();
    enterScoreFlash();
    return;
  }

  // Not eating: the tail vacates its cell, so running into it stays legal.
  for (uint8_t i = 0; i < (uint8_t)(snakeLen - 1); i++) {
    if (snake[i] == newHead) {
      gameOver();
      return;
    }
  }

  // Shift the body forward; the tail cell drops off.
  for (uint8_t i = (uint8_t)(snakeLen - 1); i > 0; i--) {
    snake[i] = snake[i - 1];
  }
  snake[0] = newHead;

  drawBoard();
}

// ============================ JOYSTICK INPUT ===============================
// HW-004 stick: VRx/VRy are analog axes (~512 centre, 0..1023 range), SW
// is a momentary button to GND (active LOW, read with INPUT_PULLUP).
// Direction is edge-detected: a tilt fires once when the stick leaves the
// dead zone, and does not fire again until it returns to centre.  This
// stops a held stick from spamming turns every loop pass.
int      joyLastDir = -1;        // -1 = centred, 0..3 = direction currently held
int      joyAxis = 0;            // sticky dominant axis: 0 = X (L/R), 1 = Y (U/D)
bool     joyBtnStable = true;    // debounced SW state (true = released)
bool     joyBtnLast   = true;    // previous raw SW sample
unsigned long joyBtnMs = 0;      // last time the raw SW sample changed

// Map the current stick position to DIR_* (or -1 when within the dead
// band around the calibrated centre).  The dominant axis is STICKY: once
// X or Y wins, the other one must beat it by JOY_AXIS_MARGIN before the
// choice flips.  Without this, a near-diagonal hold flickers between two
// decoded directions with ADC noise and the snake zigzags (R,D,R,D...).
int readJoystickDir() {
  int x = analogRead(JOY_VRX) - joyCenterX;
  int y = analogRead(JOY_VRY) - joyCenterY;
  int ax = abs(x), ay = abs(y);

  if (ax < JOY_DEADBAND && ay < JOY_DEADBAND) return -1;

  if (joyAxis == 0) {
    if (ay > ax + JOY_AXIS_MARGIN) joyAxis = 1;
  } else {
    if (ax > ay + JOY_AXIS_MARGIN) joyAxis = 0;
  }

  if (joyAxis == 0) return x < 0 ? DIR_LEFT : DIR_RIGHT;
  return y < 0 ? DIR_UP : DIR_DOWN;
}

// A stick tilt arrived: start / steer / restart, depending on the state.
void onJoystickDir(uint8_t d) {
  switch (state) {
    case STATE_PLAYING:
      setDirection(d);
      break;
    case STATE_FLASH:
      endScoreFlash();      // dismiss the score...
      setDirection(d);      // ...and keep the turn that was pressed
      break;
    case STATE_GAMEOVER:
      showEntry();          // every game ends on the entry page again
      break;
    case STATE_ENTRY:
      startGame();          // any tilt starts a round...
      setDirection(d);      // ...and the pressed direction counts right away
      break;
    default:
      break;
  }
}

// Stick button (SW) pressed: acts as a plain confirm - start a round from
// the entry page, dismiss the score flash, or leave GAME OVER.
void onJoystickButton() {
  switch (state) {
    case STATE_FLASH:
      endScoreFlash();
      break;
    case STATE_GAMEOVER:
      showEntry();
      break;
    case STATE_ENTRY:
      startGame();
      break;
    default:
      break;
  }
}

void pollJoystick(unsigned long now) {
  // ---- direction (edge-detected) ----
  int d = readJoystickDir();
  if (d != joyLastDir) {
    if (d >= 0) onJoystickDir((uint8_t)d);  // centre -> tilt: fire once
    joyLastDir = d;
  }

  // ---- button (debounced, edge-triggered) ----
  bool raw = digitalRead(JOY_SW);
  if (raw != joyBtnLast) {
    joyBtnLast = raw;
    joyBtnMs   = now;
  }
  if ((now - joyBtnMs) >= JOY_BTN_DEBOUNCE_MS && raw != joyBtnStable) {
    joyBtnStable = raw;
    if (!joyBtnStable) onJoystickButton();  // pressed (active LOW)
  }
}

// ============================ LCD STARTUP ==================================
// Finds the backpack (0x27 or 0x3F) and builds the lcd object.  Returns
// false when nothing answers on the I2C bus.
bool beginLcd() {
  const uint8_t candidates[2] = { LCD_ADDR_A, LCD_ADDR_B };
  uint8_t addr = 0;

  for (uint8_t i = 0; i < 2; i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) { addr = candidates[i]; break; }
  }
  if (addr == 0) return false;

  lcd = new LiquidCrystal_I2C(addr, LCD_COLS, LCD_ROWS);
  if (lcd == nullptr) return false;   // out of RAM: report it like a missing LCD
  lcd->init();
  lcd->backlight();

  // eight glyphs: thin snake bars, fat food bars, mixed cell, title square
  buildCellGlyphs();
  for (uint8_t i = 0; i < 8; i++) lcd->createChar(i, cellGlyphs[i]);

  Serial.print(F("LCD found at 0x"));
  Serial.println(addr, HEX);
  return true;
}

// Fast-blink D13 so a dead LCD is obvious without a Serial Monitor.
void lcdErrorBlink() {
  pinMode(LED_BUILTIN, OUTPUT);
  bool on = false;
  while (true) {
    on = !on;
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
    delay(150);
  }
}

// ============================ MAIN =========================================
void setup() {
  Serial.begin(115200);
  Serial.println(F("SNAKE - HW-004 joystick (tilt to steer, press SW to start)"));

  pinMode(JOY_SW, INPUT_PULLUP);   // SW closes to GND when the stick is pressed

  // Calibrate the stick centre (leave it untouched): average a few reads
  // so off-centre pots and tolerances don't create a phantom tilt.
  {
    long sx = 0, sy = 0;
    const uint8_t SAMPLES = 32;
    for (uint8_t i = 0; i < SAMPLES; i++) {
      sx += analogRead(JOY_VRX);
      sy += analogRead(JOY_VRY);
      delay(2);
    }
    joyCenterX = (int)(sx / SAMPLES);
    joyCenterY = (int)(sy / SAMPLES);
  }

  Wire.begin();
  if (!beginLcd()) {
    Serial.println(F("ERROR: I2C LCD not found - check SDA=A4, SCL=A5, 5V, GND"));
    lcdErrorBlink();
  }

  // A2 is left floating (nothing is connected to it): it picks up noise,
  // which is mixed with micros() so the food pattern changes every reset.
  unsigned long seed = micros();
  for (uint8_t i = 0; i < 8; i++) {
    seed = (seed << 3) ^ (seed >> 5) ^ (unsigned long)analogRead(A2);
  }
  randomSeed(seed);

  // Entry page stays on screen until the first joystick tilt / SW press
  showEntry();
}

void loop() {
  unsigned long now = millis();
  pollJoystick(now);

  switch (state) {
    case STATE_ENTRY:
      if (now - blinkMs >= BLINK_MS) {   // blink "push joystick!"
        blinkMs = now;
        blinkOn = !blinkOn;
        drawEntryHint();
      }
      break;

    case STATE_PLAYING:
      if (now - lastMoveMs >= MOVE_INTERVAL_MS) {
        lastMoveMs = now;
        moveSnake();          // may switch to FLASH or GAMEOVER
      }
      // Only blink while still playing: moveSnake() can change the state,
      // and a redraw here would wipe a GAME OVER / score screen that was
      // just drawn in this same pass through loop().
      if (state == STATE_PLAYING && now - foodBlinkMs >= FOOD_BLINK_MS) {
        foodBlinkMs = now;
        foodVisible = !foodVisible;
        drawBoard();
      }
      break;

    case STATE_FLASH:
      if (now - stateStartMs >= SCORE_FLASH_MS) endScoreFlash();
      break;

    case STATE_GAMEOVER:
      if (now - blinkMs >= BLINK_MS) {   // alternate score / restart hint
        blinkMs = now;
        blinkOn = !blinkOn;
        drawGameOverHint();
      }
      break;
  }
}
