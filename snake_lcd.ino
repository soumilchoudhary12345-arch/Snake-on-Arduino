/* ==========================================================================
 *  SNAKE  -  Arduino Uno + JHD162A 16x2 LCD (4-pin I2C version)
 * ==========================================================================
 *
 *  A small, standalone Snake game.  The whole 16x2 display is the game board
 *  (16 columns x 2 rows = 32 cells).  The LCD is a JHD162A with a mounted
 *  I2C backpack, so it only has 4 pins and needs just 2 Arduino pins.
 *
 *  The game is controlled ONLY with the arrow keys, through the Serial
 *  Monitor (115200 baud).  No push buttons are used.
 *
 *  --------------------------------------------------------------------------
 *  HARDWARE / WIRING
 *  --------------------------------------------------------------------------
 *    - Arduino Uno (or compatible)
 *    - JHD162A 16x2 LCD with 4-pin I2C backpack (PCF8574)
 *
 *    LCD pin | Name | Arduino
 *    --------+------+--------
 *      GND   | GND  | GND
 *      VCC   | VCC  | 5V
 *      SDA   | SDA  | A4
 *      SCL   | SCL  | A5
 *
 *    The small blue trimmer on the backpack sets the contrast.
 *    The backpack jumper enables the backlight.
 *
 *    I2C address: normally 0x27, on some boards 0x3F.  Both are scanned
 *    automatically at startup, so no setting is required.  If neither
 *    answers, the built-in LED (D13) blinks fast and the reason is printed
 *    on the Serial Monitor - that means the backpack is not wired right.
 *
 *    A4/A5 are the only pins the LCD uses, so D2..D13 and A0..A3 are free.
 *
 *  --------------------------------------------------------------------------
 *  HOW TO PLAY
 *  --------------------------------------------------------------------------
 *    1. Upload the sketch.
 *    2. Open the Serial Monitor at 115200 baud ("No line ending").
 *    3. The ENTRY PAGE ("SNAKE" + blinking "press arrow key") appears and
 *       stays there until you press an arrow key - that starts the game.
 *    4. Use the arrow keys of your keyboard:
 *           ^ = up,  v = down,  < = left,  > = right
 *    5. GAME OVER / YOU WIN is followed by the same entry page again;
 *       press any arrow key there to play another round.
 *
 *    Libraries (Library Manager): "LiquidCrystal I2C" by Frank de Brabander.
 * ========================================================================== */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ========================== LCD (I2C) ======================================
// Addresses tried at startup; the first one that answers wins.
const uint8_t LCD_ADDR_A = 0x27;
const uint8_t LCD_ADDR_B = 0x3F;

LiquidCrystal_I2C *lcd = nullptr;   // created in setup() once the address is known

// ========================== GAME TUNING ====================================
const uint8_t COLS = 16;                 // LCD columns
const uint8_t ROWS = 2;                  // LCD rows
const uint8_t BOARD_CELLS = COLS * ROWS; // 32 playable cells

const unsigned long MOVE_INTERVAL_MS = 150; // ms per snake cell (lower = faster)
const unsigned long SCORE_FLASH_MS   = 500; // score shown after eating food
const unsigned long BLINK_MS         = 400; // entry-page hint blink period

const uint8_t START_LENGTH  = 3;   // snake length at game start
const uint8_t FOOD_ATTEMPTS = 64;  // random tries before scanning every cell

// ========================== DIRECTIONS =====================================
// The opposite of any direction is (dir ^ 1); the arrow-key code of a
// direction is the same index, so decoding a key gives the direction.
const uint8_t DIR_UP = 0, DIR_DOWN = 1, DIR_LEFT = 2, DIR_RIGHT = 3;
const int8_t  DIR_DX[4] = { 0, 0, -1, 1 };   // column delta per direction
const int8_t  DIR_DY[4] = { -1, 1, 0, 0 };   // row delta per direction

// ========================== GAME STATES ====================================
enum GameState : uint8_t {
  STATE_ENTRY,     // entry page: "SNAKE" + blinking hint, waits for a key
  STATE_PLAYING,   // normal gameplay
  STATE_FLASH,     // score overlay after eating (short pause)
  STATE_GAMEOVER,  // collision -> "GAME OVER" + score
  STATE_WIN        // board full -> "YOU WIN!" + score
};

// ===================== CUSTOM CHARACTER GLYPHS =============================
const uint8_t GLYPH_HEAD   = 0;
const uint8_t GLYPH_BODY_A = 1;
const uint8_t GLYPH_BODY_B = 2;
const uint8_t GLYPH_FOOD   = 3;

// 5x8 pixel designs (one byte per row, low 5 bits used).
// The snake is deliberately smaller than its cell: 3 pixels wide and 5
// tall, centred, so it does not fill the whole character box.
byte glyphHead[8] = {
  0x00, // .....
  0x0E, // .###.
  0x0E, // .###.
  0x0A, // .#.#.   <- eyes
  0x0E, // .###.
  0x04, // ..#..   <- taper toward the body
  0x00, // .....
  0x00  // .....
};
byte glyphBodyA[8] = {
  0x00, // .....
  0x04, // ..#..
  0x0E, // .###.
  0x0E, // .###.
  0x0E, // .###.
  0x04, // ..#..
  0x00, // .....
  0x00  // .....
};
byte glyphBodyB[8] = {
  0x00, // .....
  0x04, // ..#..
  0x0E, // .###.
  0x0A, // .#.#.
  0x0E, // .###.
  0x04, // ..#..
  0x00, // .....
  0x00  // .....
};
byte glyphFood[8] = {
  0x00, // .....
  0x00, // .....
  0x04, // ..#..
  0x0E, // .###.
  0x04, // ..#..
  0x00, // .....
  0x00, // .....
  0x00  // .....
};

// ========================== GAME STATE DATA ================================
// (fixed-size static data only - no String, no malloc)
uint8_t snake[BOARD_CELLS];     // snake[0] = head, snake[snakeLen-1] = tail
uint8_t snakeLen = START_LENGTH;
uint8_t foodPos   = 0;          // board index of the food (0..31)
uint8_t score     = 0;
uint8_t dir       = DIR_RIGHT;  // direction the head currently moves
uint8_t pendingDir = DIR_RIGHT; // direction queued by the arrow keys
GameState state = STATE_ENTRY;

unsigned long stateStartMs = 0;  // when the current state began
unsigned long lastMoveMs   = 0;  // when the snake last moved
unsigned long blinkMs      = 0;  // when the entry-page hint toggled
bool blinkOn = true;

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

// Place food on a free cell.  Random tries first, then a full scan, so it
// always finds a spot unless the board is full (returns false -> win).
bool spawnFood() {
  for (uint8_t t = 0; t < FOOD_ATTEMPTS; t++) {
    uint8_t pos = (uint8_t)random(BOARD_CELLS);
    if (!cellOccupied(pos)) {
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

// Character to draw in board cell idx.
uint8_t cellGlyph(uint8_t idx) {
  if (idx == foodPos) return GLYPH_FOOD;
  for (uint8_t i = 0; i < snakeLen; i++) {
    if (snake[i] == idx) {
      if (i == 0) return GLYPH_HEAD;
      return (i & 1) ? GLYPH_BODY_B : GLYPH_BODY_A;
    }
  }
  return (uint8_t)' ';
}

// Full redraw of both rows.  Unchanged cells get the same character again,
// which is visually identical, so there is no flicker.
void drawBoard() {
  for (uint8_t row = 0; row < ROWS; row++) {
    lcd->setCursor(0, row);
    uint8_t base = row * COLS;
    for (uint8_t col = 0; col < COLS; col++) {
      lcd->write(cellGlyph(base + col));
    }
  }
}

// ============================ SCREENS ======================================
// ---------------- ENTRY PAGE (shown at boot and after every game) ----------
void drawEntryHint() {
  lcd->setCursor(0, 1);
  if (blinkOn) {
    lcd->print(F("press arrow key"));        // 15 chars -> cols 0..14
  } else {
    for (uint8_t i = 0; i < COLS; i++) lcd->write(' ');
  }
}

void showEntry() {
  state = STATE_ENTRY;
  lcd->clear();
  lcd->setCursor(5, 0);                       // "SNAKE" = 5 chars -> col 5
  lcd->print(F("SNAKE"));
  blinkOn = true;
  blinkMs = millis();
  drawEntryHint();
  Serial.println(F("Entry page - press an arrow key to start"));
}

void printScoreLine() {
  // "Score: " = 7 chars + 1 or 2 digits -> centered on the bottom row
  uint8_t len = (score < 10) ? 8 : 9;
  lcd->setCursor((COLS - len) / 2, 1);
  lcd->print(F("Score: "));
  lcd->print(score);
}

void gameOver() {
  state = STATE_GAMEOVER;
  lcd->clear();
  lcd->setCursor(3, 0);                 // "GAME OVER" = 9 chars -> col 3
  lcd->print(F("GAME OVER"));
  printScoreLine();
  Serial.println(F("GAME OVER - press an arrow key to go back to the title"));
}

void showWin() {
  state = STATE_WIN;
  lcd->clear();
  lcd->setCursor(4, 0);                 // "YOU WIN!" = 8 chars -> col 4
  lcd->print(F("YOU WIN!"));
  printScoreLine();
  Serial.println(F("YOU WIN! - press an arrow key to go back to the title"));
}

// Score overlay on the TOP row only: the bottom row still shows the board.
void enterScoreFlash() {
  state = STATE_FLASH;
  stateStartMs = millis();
  lcd->setCursor(0, 0);
  for (uint8_t i = 0; i < COLS; i++) lcd->write(' ');
  uint8_t len = (score < 10) ? 8 : 9;
  lcd->setCursor((COLS - len) / 2, 0);
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

  // 2) self collision - when not eating the tail frees up this tick, so
  //    running into the current tail is legal
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

// ============================ ARROW-KEY INPUT ==============================
// The Serial Monitor sends the usual ANSI escape sequence for an arrow key:
//     ESC  '['  'A' (up) / 'B' (down) / 'C' (right) / 'D' (left)
// Everything else (newlines, stray characters) is ignored.
const unsigned long ESC_TIMEOUT_MS = 50;
uint8_t        escState = 0;   // 0 = idle, 1 = saw ESC, 2 = saw ESC '['
unsigned long  escAtMs  = 0;

// An arrow key arrived: start / steer / restart, depending on the state.
void onArrowKey(uint8_t d) {
  switch (state) {
    case STATE_PLAYING:
      setDirection(d);
      break;
    case STATE_FLASH:
      endScoreFlash();      // dismiss the score...
      setDirection(d);      // ...and keep the turn that was pressed
      break;
    case STATE_GAMEOVER:
    case STATE_WIN:
      showEntry();          // every game ends on the entry page again
      break;
    case STATE_ENTRY:
      startGame();          // any arrow key starts a round...
      setDirection(d);      // ...and the pressed direction counts right away
      break;
    default:
      break;
  }
}

void pollSerial(unsigned long now) {
  if (escState && (now - escAtMs) >= ESC_TIMEOUT_MS) escState = 0;

  while (Serial.available()) {
    char c = (char)Serial.read();

    if (escState == 2) {              // third byte of the arrow sequence
      escState = 0;
      switch (c) {
        case 'A': onArrowKey(DIR_UP);    break;
        case 'B': onArrowKey(DIR_DOWN);  break;
        case 'C': onArrowKey(DIR_RIGHT); break;
        case 'D': onArrowKey(DIR_LEFT);  break;
        default:  break;
      }
      continue;
    }
    if (c == (char)0x1B) { escState = 1; escAtMs = now; continue; }
    if (escState == 1) {              // expected '[' but got something else
      escState = 0;
      if (c != '[') continue;
      escState = 2;
      continue;
    }
    // any byte outside a sequence is not an arrow key -> ignored
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

  lcd = new LiquidCrystal_I2C(addr, COLS, ROWS);
  lcd->init();
  lcd->backlight();
  lcd->createChar(GLYPH_HEAD, glyphHead);
  lcd->createChar(GLYPH_BODY_A, glyphBodyA);
  lcd->createChar(GLYPH_BODY_B, glyphBodyB);
  lcd->createChar(GLYPH_FOOD, glyphFood);

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
  Serial.println(F("SNAKE - arrow keys only (115200, no line ending)"));

  Wire.begin();
  if (!beginLcd()) {
    Serial.println(F("ERROR: I2C LCD not found - check SDA=A4, SCL=A5, 5V, GND"));
    lcdErrorBlink();
  }

  // A0 is left floating (nothing is connected to it): it picks up noise,
  // which is mixed with micros() so the food pattern changes every reset.
  unsigned long seed = micros();
  for (uint8_t i = 0; i < 8; i++) {
    seed = (seed << 3) ^ (seed >> 5) ^ (unsigned long)analogRead(A0);
  }
  randomSeed(seed);

  // Entry page stays on screen until the first arrow key is pressed
  showEntry();
}

void loop() {
  unsigned long now = millis();
  pollSerial(now);

  switch (state) {
    case STATE_ENTRY:
      if (now - blinkMs >= BLINK_MS) {   // blink "press arrow key"
        blinkMs = now;
        blinkOn = !blinkOn;
        drawEntryHint();
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
      break;                        // wait for an arrow key
  }
}
