/* ==========================================================================
 *  SNAKE  -  Arduino Uno + JHD162A 16x2 character LCD
 * ==========================================================================
 *
 *  A complete, standalone Snake game.  The whole 16x2 display is used as
 *  the game board (16 columns x 2 rows = 32 cells).  Only the standard
 *  LiquidCrystal library (ships with the Arduino IDE) is required.
 *
 *  --------------------------------------------------------------------------
 *  REQUIRED HARDWARE
 *  --------------------------------------------------------------------------
 *    - Arduino Uno (or compatible)
 *    - JHD162A 16x2 LCD, HD44780-compatible, 4-bit interface
 *    - 4x momentary push buttons (tactile switches)
 *    - 1x 10k potentiometer (LCD contrast)  [or two 1k resistors]
 *    - 1x 220 - 330 ohm resistor (backlight)
 *    - Breadboard + jumper wires
 *
 *  --------------------------------------------------------------------------
 *  WIRING
 *  --------------------------------------------------------------------------
 *
 *  LCD connector (JHD162A, 16 pins, numbered 1..16 left to right)
 *
 *   LCD pin | Name | Connects to             | Purpose
 *   --------+------+-------------------------+---------------------------
 *      1    | VSS  | Arduino GND             | Ground
 *      2    | VDD  | Arduino 5V              | +5 V power
 *      3    | VO   | Potentiometer wiper     | Contrast (see below)
 *      4    | RS   | Arduino pin 12          | Register select
 *      5    | RW   | Arduino GND             | Read/Write: tie LOW (write)
 *      6    | E    | Arduino pin 11          | Enable
 *      7    | D0   | not connected           | 4-bit mode -> D0..D3 unused
 *      8    | D1   | not connected           |
 *      9    | D2   | not connected           |
 *     10    | D3   | not connected           |
 *     11    | D4   | Arduino pin 5           | Data bit 4
 *     12    | D5   | Arduino pin 4           | Data bit 5
 *     13    | D6   | Arduino pin 3           | Data bit 6
 *     14    | D7   | Arduino pin 2           | Data bit 7
 *     15    | A    | 5V -- 220R -- pin 15    | Backlight anode (+)
 *     16    | K    | Arduino GND             | Backlight cathode (-)
 *
 *  Contrast potentiometer (10k):
 *
 *      5V ---[ potentiometer ]--- GND
 *                   |
 *                wiper  --->  LCD pin 3 (VO)
 *
 *      (No pot?  5V -- 1k -- VO -- 1k -- GND also gives a usable contrast.)
 *
 *  Buttons - one leg to the Arduino pin, the other leg to GND.
 *  Internal pull-ups are enabled in software (INPUT_PULLUP), so NO external
 *  resistors are required.  A pressed button reads LOW.
 *
 *   Button | Arduino pin | Other leg | Purpose
 *   -------+-------------+-----------+---------
 *     UP   | A0          | GND       | move up
 *     DOWN | A1          | GND       | move down
 *     LEFT | A2          | GND       | move left
 *     RIGHT| A3          | GND       | move right
 *
 *  ALL BUTTONS ARE OPTIONAL.  The game can be played entirely over USB
 *  serial - open the Serial Monitor at 115200 baud and send w / a / s / d
 *  (or the arrow keys).  Unconnected button pins read HIGH with the
 *  internal pull-ups, so they are simply ignored.
 *
 *  --------------------------------------------------------------------------
 *  DESIGN NOTES
 *  --------------------------------------------------------------------------
 *  * HD44780 limitation: a 16x2 LCD has only 32 visible character cells and
 *    8 custom-character slots (0..7).  There is no pixel-level drawing, so
 *    the game board IS the character grid: one cell of the board equals one
 *    character cell of the LCD.
 *
 *  * Board representation: every cell is a single byte "index"
 *        index = row * 16 + col     (row = 0..1, col = 0..15, index = 0..31)
 *    This keeps the whole board in one byte per snake segment (32 bytes max)
 *    and makes bounds/occupancy checks trivial.
 *
 *  * The snake is an ordered byte array: snake[0] = head, snake[len-1] =
 *    tail.  Moving = shift the array one slot and insert the new head.
 *    At <= 32 bytes this is cheaper (RAM-wise) than any linked structure
 *    and needs no dynamic memory.
 *
 *  * Graphics: 4 of the 8 custom slots are used
 *        slot 0 = head, slot 1/2 = alternating body segments, slot 3 = food.
 *    If the custom characters do not render on your LCD, hold RIGHT while
 *    powering on - the game then falls back to plain ASCII (@ # *).
 *
 *  * Timing: everything runs from millis() in loop(); delay() is never used,
 *    so buttons stay responsive.  Buttons are sampled every loop and
 *    debounced with a 30 ms stability window (INPUT_PULLUP, active LOW).
 *
 *  * Score: the board fills the display completely, so the score cannot be
 *    shown permanently.  It is flashed for 500 ms on the top row whenever
 *    food is eaten (game pauses meanwhile, input stays buffered), and it is
 *    shown on the GAME OVER / WIN screens.
 *
 *  * Direction reversal: direction codes are chosen so that the opposite of
 *    any direction is (dir ^ 1); attempts to reverse 180 degrees are
 *    discarded by setDirection().
 * ========================================================================== */

#include <LiquidCrystal.h>

// ===================== HARDWARE PIN DEFINITIONS ===========================
// (change these only - everything else follows)

const uint8_t PIN_LCD_RS = 12;   // LCD RS
const uint8_t PIN_LCD_E  = 11;   // LCD E
const uint8_t PIN_LCD_D4 = 5;    // LCD D4
const uint8_t PIN_LCD_D5 = 4;    // LCD D5
const uint8_t PIN_LCD_D6 = 3;    // LCD D6
const uint8_t PIN_LCD_D7 = 2;    // LCD D7

const uint8_t PIN_BTN_UP    = A0;
const uint8_t PIN_BTN_DOWN  = A1;
const uint8_t PIN_BTN_LEFT  = A2;
const uint8_t PIN_BTN_RIGHT = A3;

LiquidCrystal lcd(PIN_LCD_RS, PIN_LCD_E, PIN_LCD_D4, PIN_LCD_D5,
                  PIN_LCD_D6, PIN_LCD_D7);

// ========================== GAME TUNING ====================================
const uint8_t COLS = 16;                 // board columns (LCD columns)
const uint8_t ROWS = 2;                  // board rows    (LCD rows)
const uint8_t BOARD_CELLS = COLS * ROWS; // 32 playable cells

const unsigned long MOVE_INTERVAL_MS = 150; // speed: ms per snake cell (lower = faster)
const unsigned long DEBOUNCE_MS      = 30;  // button debounce window
const unsigned long TITLE_MS         = 1500;// "SNAKE" shown this long at power-on
const unsigned long SCORE_FLASH_MS   = 500; // score shown after eating food
const unsigned long BLINK_MS         = 400; // prompt blink period

const uint8_t START_LENGTH  = 3;   // snake length at game start
const uint8_t FOOD_ATTEMPTS = 64;  // random tries before scanning every cell
const bool    USE_CUSTOM_CHARS_DEFAULT = true; // false = ASCII graphics only

// ========================== DIRECTIONS =====================================
// Order matters: UP/DOWN share bit 0, LEFT/RIGHT share bit 0, therefore the
// opposite direction is always (dir ^ 1).  Button indices (see BTN_PINS
// below) use the same order, so button index == direction code.
const uint8_t DIR_UP = 0, DIR_DOWN = 1, DIR_LEFT = 2, DIR_RIGHT = 3;
const int8_t  DIR_DX[4] = { 0, 0, -1, 1 };   // column delta per direction
const int8_t  DIR_DY[4] = { -1, 1, 0, 0 };   // row delta per direction

// ========================== GAME STATES ====================================
enum GameState : uint8_t {
  STATE_TITLE,     // "SNAKE" splash
  STATE_PROMPT,    // "Press any key" (blinking), waits for a button
  STATE_PLAYING,   // normal gameplay
  STATE_FLASH,     // score overlay after eating (short, non-blocking pause)
  STATE_GAMEOVER,  // collision -> "GAME OVER" + score, waits for a button
  STATE_WIN        // snake filled the board -> "YOU WIN!" + score
};

// ===================== CUSTOM CHARACTER GLYPHS =============================
// 4 of the 8 available HD44780 slots are used (0..3).
const uint8_t GLYPH_HEAD   = 0;
const uint8_t GLYPH_BODY_A = 1;
const uint8_t GLYPH_BODY_B = 2;
const uint8_t GLYPH_FOOD   = 3;

// Plain-ASCII fallback (used when custom chars are disabled)
const uint8_t ASCII_HEAD   = '@';
const uint8_t ASCII_BODY_A = '#';
const uint8_t ASCII_BODY_B = '#';
const uint8_t ASCII_FOOD   = '*';

// 5x8 pixel designs (one byte per row, low 5 bits used)
byte glyphHead[8] = {
  0x0E, // .###.
  0x1F, // #####
  0x17, // #.###   <- eye
  0x1F, // #####
  0x1F, // #####
  0x1F, // #####
  0x1F, // #####
  0x0E  // .###.
};
byte glyphBodyA[8] = {
  0x0E, // .###.
  0x1F, // #####
  0x1F, // #####
  0x1F, // #####
  0x1F, // #####
  0x1F, // #####
  0x1F, // #####
  0x0E  // .###.
};
byte glyphBodyB[8] = {
  0x0E, // .###.
  0x1F, // #####
  0x1B, // ##.##
  0x1F, // #####
  0x1F, // #####
  0x1B, // ##.##
  0x1F, // #####
  0x0E  // .###.
};
byte glyphFood[8] = {
  0x00, // .....
  0x04, // ..#..
  0x0E, // .###.
  0x1F, // #####
  0x1F, // #####
  0x0E, // .###.
  0x04, // ..#..
  0x00  // .....
};

// ========================== GAME STATE DATA ================================
// (fixed-size static data only - no String, no malloc)
uint8_t snake[BOARD_CELLS];    // snake[0] = head, snake[snakeLen-1] = tail
uint8_t snakeLen = START_LENGTH;
uint8_t foodPos   = 0;         // board index of the food (0..31)
uint8_t score     = 0;
uint8_t dir       = DIR_RIGHT; // direction the head currently moves
uint8_t pendingDir = DIR_RIGHT;// direction queued by the buttons
GameState state = STATE_TITLE;
bool useCustomChars = USE_CUSTOM_CHARS_DEFAULT;

unsigned long stateStartMs = 0;  // when the current state began
unsigned long lastMoveMs   = 0;  // when the snake last moved
unsigned long blinkMs      = 0;  // prompt blink timer
bool blinkOn = true;

// ========================== BUTTON DEBOUNCING ==============================
const uint8_t NUM_BTNS = 4;
// Order MUST be UP, DOWN, LEFT, RIGHT so index == direction code.
const uint8_t BTN_PINS[NUM_BTNS] = { PIN_BTN_UP, PIN_BTN_DOWN,
                                     PIN_BTN_LEFT, PIN_BTN_RIGHT };
bool     btnPressed[NUM_BTNS];      // debounced state (true = held down)
bool     btnRaw[NUM_BTNS];          // last raw sample
unsigned long btnLastChange[NUM_BTNS];

// ============================ HELPERS ======================================
uint8_t colOf(uint8_t idx) { return idx & 0x0F; }   // board index -> column
uint8_t rowOf(uint8_t idx) { return idx >> 4; }     // board index -> row

// True if any snake segment occupies this board cell.
bool cellOccupied(uint8_t pos) {
  for (uint8_t i = 0; i < snakeLen; i++) {
    if (snake[i] == pos) return true;
  }
  return false;
}

// Place food on a free, in-bounds cell.  Tries random positions first,
// then scans every cell, so it ALWAYS finds a spot unless the board is
// completely full (returns false only in that case -> win).
bool spawnFood() {
  for (uint8_t t = 0; t < FOOD_ATTEMPTS; t++) {
    uint8_t pos = (uint8_t)random(BOARD_CELLS);
    if (pos < BOARD_CELLS && !cellOccupied(pos)) {  // robustness double-check
      foodPos = pos;
      return true;
    }
  }
  for (uint8_t pos = 0; pos < BOARD_CELLS; pos++) {
    if (!cellOccupied(pos)) {
      foodPos = pos;
      return true;
    }
  }
  return false;
}

// Character to draw in board cell idx (custom glyph or ASCII fallback).
uint8_t cellGlyph(uint8_t idx) {
  if (idx == foodPos) return useCustomChars ? GLYPH_FOOD : ASCII_FOOD;
  for (uint8_t i = 0; i < snakeLen; i++) {
    if (snake[i] == idx) {
      if (i == 0) return useCustomChars ? GLYPH_HEAD : ASCII_HEAD;
      if (i & 1)  return useCustomChars ? GLYPH_BODY_B : ASCII_BODY_B;
      return useCustomChars ? GLYPH_BODY_A : ASCII_BODY_A;
    }
  }
  return (uint8_t)' ';
}

// Full redraw of both rows.  Unchanged cells receive the same character
// again, which is visually identical - so there is no flicker.
void drawBoard() {
  for (uint8_t row = 0; row < ROWS; row++) {
    lcd.setCursor(0, row);
    uint8_t base = row * COLS;
    for (uint8_t col = 0; col < COLS; col++) {
      lcd.write(cellGlyph(base + col));
    }
  }
}

// ============================ SCREENS ======================================
void drawPromptText() {
  // "Press any key" is 13 chars -> centered at column 1
  lcd.setCursor(1, 0);
  if (blinkOn) {
    lcd.print(F("Press any key"));
  } else {
    for (uint8_t i = 0; i < 13; i++) lcd.write(' ');
  }
}

void showPrompt() {
  state = STATE_PROMPT;
  lcd.clear();
  blinkOn = true;
  blinkMs = millis();
  drawPromptText();
}

void printScoreLine() {
  // "Score: " = 7 chars + 1 or 2 digits -> centered
  uint8_t len = (score < 10) ? 8 : 9;
  lcd.setCursor((COLS - len) / 2, 1);
  lcd.print(F("Score: "));
  lcd.print(score);
}

void gameOver() {
  state = STATE_GAMEOVER;
  lcd.clear();
  lcd.setCursor(3, 0);                 // "GAME OVER" = 9 chars -> col 3
  lcd.print(F("GAME OVER"));
  printScoreLine();
}

void showWin() {
  state = STATE_WIN;
  lcd.clear();
  lcd.setCursor(4, 0);                 // "YOU WIN!" = 8 chars -> col 4
  lcd.print(F("YOU WIN!"));
  printScoreLine();
}

// Score overlay: written over the TOP row only, so the bottom row still
// shows part of the board while the game is paused.
void enterScoreFlash() {
  state = STATE_FLASH;
  stateStartMs = millis();
  lcd.setCursor(0, 0);
  for (uint8_t i = 0; i < COLS; i++) lcd.write(' ');
  uint8_t len = (score < 10) ? 8 : 9;
  lcd.setCursor((COLS - len) / 2, 0);
  lcd.print(F("Score: "));
  lcd.print(score);
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

  // Head on row 0, column 5; body trailing to the left (index = row*16+col)
  snake[0] = 5;
  snake[1] = 4;
  snake[2] = 3;

  state = STATE_PLAYING;
  lastMoveMs = millis();

  if (!spawnFood()) { showWin(); return; }  // practically impossible here
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

  // 1) wall / boundary collision
  if (newCol < 0 || newCol >= COLS || newRow < 0 || newRow >= ROWS) {
    gameOver();
    return;
  }

  uint8_t newHead = (uint8_t)(newRow * COLS + newCol);
  bool eating = (newHead == foodPos);

  // 2) self collision - when not eating, the tail cell frees up this tick,
  //    so running into the current tail is legal.
  uint8_t checkCount = eating ? snakeLen : (uint8_t)(snakeLen - 1);
  for (uint8_t i = 0; i < checkCount; i++) {
    if (snake[i] == newHead) {
      gameOver();
      return;
    }
  }

  // grow first when eating so the old tail is shifted, not dropped
  if (eating && snakeLen < BOARD_CELLS) snakeLen++;

  for (uint8_t i = (uint8_t)(snakeLen - 1); i > 0; i--) {
    snake[i] = snake[i - 1];
  }
  snake[0] = newHead;

  if (eating) {
    score++;
    if (!spawnFood()) { showWin(); return; }  // board full -> win
    enterScoreFlash();
  } else {
    drawBoard();
  }
}

// ============================ INPUT ========================================
// Classic per-pin debounce: a state change is accepted only after it has
// been stable for DEBOUNCE_MS.  onButtonPress() fires on the pressed edge.
void pollButtons(unsigned long now) {
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    bool raw = (digitalRead(BTN_PINS[i]) == LOW);
    if (raw != btnRaw[i]) {
      btnRaw[i] = raw;
      btnLastChange[i] = now;
    }
    if (raw != btnPressed[i] && (now - btnLastChange[i]) >= DEBOUNCE_MS) {
      btnPressed[i] = raw;
      if (btnPressed[i]) onButtonPress(i);
    }
  }
}

void onButtonPress(uint8_t idx) {   // idx: 0=UP 1=DOWN 2=LEFT 3=RIGHT
  switch (state) {
    case STATE_PROMPT:
      startGame();                  // any button starts the game
      break;
    case STATE_PLAYING:
      setDirection(idx);
      break;
    case STATE_FLASH:
      endScoreFlash();              // any button dismisses the score...
      setDirection(idx);            // ...and the direction is buffered
      break;
    case STATE_GAMEOVER:
    case STATE_WIN:
      startGame();                  // any button restarts
      break;
    case STATE_TITLE:
    default:
      break;                        // splash ignores input
  }
}

// ============================ SERIAL INPUT ================================
// Buttons are optional: the same events are accepted over USB serial at
// 115200 baud.  w/a/s/d (any case) map to UP/LEFT/DOWN/RIGHT; the ANSI
// arrow-key sequences (ESC '[' 'A'..'D') are decoded too, so the arrow
// keys of the Serial Monitor work.  Anything else (newline, garbage) is
// ignored.
const unsigned long ESC_TIMEOUT_MS = 50;
uint8_t        escState = 0;   // 0 = idle, 1 = saw ESC, 2 = saw ESC '['
unsigned long  escAtMs  = 0;

void pollSerial(unsigned long now) {
  if (escState && (now - escAtMs) >= ESC_TIMEOUT_MS) escState = 0;

  while (Serial.available()) {
    char c = (char)Serial.read();

    if (escState == 2) {              // third byte of an arrow-key sequence
      escState = 0;
      switch (c) {
        case 'A': onButtonPress(DIR_UP);    break;
        case 'B': onButtonPress(DIR_DOWN);  break;
        case 'C': onButtonPress(DIR_RIGHT); break;
        case 'D': onButtonPress(DIR_LEFT);  break;
        default:  break;
      }
      continue;
    }
    if (c == (char)0x1B) { escState = 1; escAtMs = now; continue; }
    if (escState == 1) {              // expected '[' but got something else
      escState = 0;
      if (c != '[') continue;         // lone ESC or malformed sequence: drop
      escState = 2;
      continue;
    }

    switch (c) {
      case 'w': case 'W': onButtonPress(DIR_UP);    break;
      case 's': case 'S': onButtonPress(DIR_DOWN);  break;
      case 'a': case 'A': onButtonPress(DIR_LEFT);  break;
      case 'd': case 'D': onButtonPress(DIR_RIGHT); break;
      default:  break;                // '\n', '\r', stray chars: ignore
    }
  }
}

// ============================ MAIN =========================================
void setup() {
  Serial.begin(115200);
  Serial.println(F("SNAKE: send w/a/s/d or arrow keys to play"));

  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);   // buttons: pin <-> GND, no resistors
  }

  lcd.begin(COLS, ROWS);

  // Hold RIGHT while powering on / resetting to force plain-ASCII graphics
  // in case the custom characters do not render on your particular LCD.
  useCustomChars = USE_CUSTOM_CHARS_DEFAULT;
  if (digitalRead(PIN_BTN_RIGHT) == LOW) useCustomChars = false;

  if (useCustomChars) {
    lcd.createChar(GLYPH_HEAD, glyphHead);
    lcd.createChar(GLYPH_BODY_A, glyphBodyA);
    lcd.createChar(GLYPH_BODY_B, glyphBodyB);
    lcd.createChar(GLYPH_FOOD, glyphFood);
  }

  // A4 is intentionally left floating: it picks up noise, mixed with
  // micros(), so food placement differs from power-on to power-on.
  unsigned long seed = ((unsigned long)analogRead(A4) << 16) ^ micros();
  randomSeed(seed);

  // Sample buttons once so a button already held at boot is not seen as
  // a fresh press (only a new pressed edge triggers onButtonPress()).
  unsigned long now = millis();
  for (uint8_t i = 0; i < NUM_BTNS; i++) {
    btnRaw[i] = (digitalRead(BTN_PINS[i]) == LOW);
    btnPressed[i] = btnRaw[i];
    btnLastChange[i] = now;
  }

  // Startup screen: "SNAKE"
  lcd.clear();
  lcd.setCursor(5, 0);              // "SNAKE" = 5 chars -> col 5
  lcd.print(F("SNAKE"));
  state = STATE_TITLE;
  stateStartMs = now;
}

void loop() {
  unsigned long now = millis();
  pollButtons(now);                 // input runs every pass -> no delay()
  pollSerial(now);                  // USB serial steering (no buttons needed)

  switch (state) {
    case STATE_TITLE:
      if (now - stateStartMs >= TITLE_MS) showPrompt();
      break;

    case STATE_PROMPT:
      if (now - blinkMs >= BLINK_MS) {
        blinkMs = now;
        blinkOn = !blinkOn;
        drawPromptText();
      }
      break;

    case STATE_PLAYING:
      if (now - lastMoveMs >= MOVE_INTERVAL_MS) {
        lastMoveMs = now;
        moveSnake();
      }
      break;

    case STATE_FLASH:
      if (now - stateStartMs >= SCORE_FLASH_MS) endScoreFlash();
      break;

    case STATE_GAMEOVER:
    case STATE_WIN:
      break;                        // wait for a button press
  }
}
