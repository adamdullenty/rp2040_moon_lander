// Moon Lander for Adafruit Feather RP2040 + 128x64 OLED FeatherWing (SH1107)
//
// Controls:
//   A     = rotate left
//   C     = rotate right
//   B     = thrust (burns fuel)
//   A/B/C = start / retry from title or result screen
//
// Land on a flat pad slowly and upright. Too fast, tilted, or on rough
// ground = crater.
//
// Libraries: Adafruit SH110x, Adafruit GFX, Adafruit NeoPixel, Adafruit BusIO
// Board: Adafruit Feather RP2040 (Earle Philhower core)

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

#define BUTTON_A 9
#define BUTTON_B 8
#define BUTTON_C 7

#ifndef PIN_NEOPIXEL
#define PIN_NEOPIXEL 16
#endif

static const int SCREEN_W = 128;
static const int SCREEN_H = 64;
static const float DEG = 0.0174533f;
static const float PI2 = 6.2831853f;

// World: y+ is down (screen space)
static const float GRAVITY = 0.012f;
static const float THRUST_ACCEL = 0.028f;
static const float ROT_SPEED = 2.8f * DEG;  // per frame-unit
static const float MAX_LAND_VY = 0.55f;
static const float MAX_LAND_VX = 0.35f;
static const float MAX_LAND_ANGLE = 18.0f * DEG;  // from upright (-90°)
static const float FUEL_MAX = 100.0f;
static const float FUEL_BURN = 0.35f;  // per thrust frame-unit

Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

enum GameState : uint8_t { TITLE, FLYING, LANDED, CRASHED };
GameState state = TITLE;

struct Lander {
  float x, y;
  float vx, vy;
  float angle;  // radians; -PI/2 = nose up
  float fuel;
  bool thrusting;
};

static const uint8_t GROUND_N = 32;  // columns of terrain (4 px each)
int8_t groundY[GROUND_N];            // top of ground in pixels
bool padFlat[GROUND_N];

Lander ship;
uint16_t score = 0;
uint16_t highScore = 0;
uint8_t padLeft = 0, padRight = 0;  // inclusive ground columns

unsigned long lastFrame = 0;
unsigned long lastPixel = 0;
unsigned long inputLockUntil = 0;
uint16_t hue = 0;
uint8_t crashTick = 0;

bool aDown = false, bDown = false, cDown = false;

void setNeo(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

float wrapAngle(float a) {
  while (a < -PI2 * 0.5f) a += PI2;
  while (a > PI2 * 0.5f) a -= PI2;
  return a;
}

// Upright nose-up is -PI/2
float tiltFromUpright(float angle) {
  float upright = -PI2 * 0.25f;
  float d = angle - upright;
  while (d > PI2 * 0.5f) d -= PI2;
  while (d < -PI2 * 0.5f) d += PI2;
  return fabsf(d);
}

void generateTerrain() {
  // Rolling hills via low-freq random walk
  int y = 48;
  for (uint8_t i = 0; i < GROUND_N; i++) {
    y += (int)(rp2040.hwrand32() % 5) - 2;
    if (y < 36) y = 36;
    if (y > 58) y = 58;
    groundY[i] = (int8_t)y;
    padFlat[i] = false;
  }

  // Carve one flat landing pad (3–5 columns)
  uint8_t padW = 3 + (rp2040.hwrand32() % 3);
  padLeft = 2 + (rp2040.hwrand32() % (GROUND_N - padW - 4));
  padRight = padLeft + padW - 1;
  int8_t padY = groundY[padLeft];
  for (uint8_t i = padLeft; i <= padRight; i++) {
    groundY[i] = padY;
    padFlat[i] = true;
  }
  // Soft shoulders
  if (padLeft > 0) groundY[padLeft - 1] = (groundY[padLeft - 1] + padY) / 2;
  if (padRight + 1 < GROUND_N) groundY[padRight + 1] = (groundY[padRight + 1] + padY) / 2;
}

int groundAtPixelX(float x) {
  int col = (int)(x / 4.0f);
  if (col < 0) col = 0;
  if (col >= GROUND_N) col = GROUND_N - 1;
  return groundY[col];
}

bool onPad(float x) {
  int col = (int)(x / 4.0f);
  if (col < 0 || col >= GROUND_N) return false;
  return padFlat[col];
}

void resetShip() {
  ship.x = 20.0f + (rp2040.hwrand32() % 90);
  ship.y = 8.0f;
  ship.vx = ((int)(rp2040.hwrand32() % 7) - 3) * 0.08f;
  ship.vy = 0.05f;
  ship.angle = -PI2 * 0.25f;
  ship.fuel = FUEL_MAX;
  ship.thrusting = false;
}

void startGame() {
  generateTerrain();
  resetShip();
  state = FLYING;
  crashTick = 0;
}

void handleInput(float dt) {
  bool a = digitalRead(BUTTON_A) == LOW;
  bool b = digitalRead(BUTTON_B) == LOW;
  bool c = digitalRead(BUTTON_C) == LOW;
  bool acceptStart = millis() >= inputLockUntil;

  ship.thrusting = false;

  if ((state == TITLE || state == LANDED || state == CRASHED) && acceptStart) {
    if ((a && !aDown) || (b && !bDown) || (c && !cDown)) {
      startGame();
    }
  } else if (state == FLYING) {
    float rot = ROT_SPEED * (dt * 60.0f);
    if (a) ship.angle = wrapAngle(ship.angle - rot);
    if (c) ship.angle = wrapAngle(ship.angle + rot);
    if (b && ship.fuel > 0.0f) {
      ship.thrusting = true;
      float burn = FUEL_BURN * (dt * 60.0f);
      if (burn > ship.fuel) burn = ship.fuel;
      ship.fuel -= burn;
      float t = THRUST_ACCEL * (dt * 60.0f);
      // Thrust along nose direction (angle points to nose)
      ship.vx += cosf(ship.angle) * t;
      ship.vy += sinf(ship.angle) * t;
    }
  }

  aDown = a;
  bDown = b;
  cDown = c;
}

void updateFlight(float dt) {
  if (state != FLYING) return;

  float scale = dt * 60.0f;
  ship.vy += GRAVITY * scale;
  // light drag so it doesn't feel floaty forever
  ship.vx *= powf(0.995f, scale);
  ship.vy *= powf(0.998f, scale);

  ship.x += ship.vx * scale;
  ship.y += ship.vy * scale;

  // Keep on screen horizontally (bounce gently)
  if (ship.x < 3.0f) { ship.x = 3.0f; ship.vx = fabsf(ship.vx) * 0.4f; }
  if (ship.x > SCREEN_W - 3.0f) { ship.x = SCREEN_W - 3.0f; ship.vx = -fabsf(ship.vx) * 0.4f; }

  // Ceiling
  if (ship.y < 2.0f) { ship.y = 2.0f; ship.vy = fabsf(ship.vy) * 0.3f; }

  float gy = (float)groundAtPixelX(ship.x);
  // Ship "feet" a few px below center
  float feet = ship.y + 4.0f;
  if (feet >= gy) {
    ship.y = gy - 4.0f;
    bool goodPad = onPad(ship.x);
    bool soft = (ship.vy <= MAX_LAND_VY) && (fabsf(ship.vx) <= MAX_LAND_VX);
    bool upright = tiltFromUpright(ship.angle) <= MAX_LAND_ANGLE;

    if (goodPad && soft && upright) {
      float landVy = ship.vy;
      ship.vx = 0;
      ship.vy = 0;
      state = LANDED;
      uint16_t s = (uint16_t)ship.fuel + (uint16_t)((MAX_LAND_VY - landVy) * 40.0f);
      if (s < 10) s = 10;
      score = s;
      if (score > highScore) highScore = score;
    } else {
      state = CRASHED;
      crashTick = 0;
      score = 0;
    }
  }
}

void drawTerrain() {
  for (uint8_t i = 0; i < GROUND_N - 1; i++) {
    int16_t x0 = i * 4;
    int16_t x1 = (i + 1) * 4;
    display.drawLine(x0, groundY[i], x1, groundY[i + 1], SH110X_WHITE);
  }
  // Fill below ground with sparse dust (cheap texture)
  for (uint8_t i = 0; i < GROUND_N; i++) {
    int16_t x = i * 4 + 1;
    for (int16_t y = groundY[i] + 2; y < SCREEN_H; y += 3) {
      if (((i + y) & 1) == 0) display.drawPixel(x, y, SH110X_WHITE);
    }
  }
  // Mark pad with double line
  for (uint8_t i = padLeft; i <= padRight; i++) {
    int16_t x0 = i * 4;
    display.drawFastHLine(x0, groundY[i] - 1, 4, SH110X_WHITE);
  }
}

void drawLander() {
  float c = cosf(ship.angle), s = sinf(ship.angle);
  auto tx = [&](float lx, float ly) -> int16_t {
    return (int16_t)(ship.x + lx * c - ly * s + 0.5f);
  };
  auto ty = [&](float lx, float ly) -> int16_t {
    return (int16_t)(ship.y + lx * s + ly * c + 0.5f);
  };

  // Capsule body + landing legs
  int16_t noseX = tx(4.0f, 0), noseY = ty(4.0f, 0);
  int16_t lX = tx(-3.0f, -3.0f), lY = ty(-3.0f, -3.0f);
  int16_t rX = tx(-3.0f,  3.0f), rY = ty(-3.0f,  3.0f);
  int16_t legLX = tx(-5.0f, -4.5f), legLY = ty(-5.0f, -4.5f);
  int16_t legRX = tx(-5.0f,  4.5f), legRY = ty(-5.0f,  4.5f);

  if (state == CRASHED) {
    // Debris cloud
    for (uint8_t i = 0; i < 10; i++) {
      int16_t dx = (int16_t)((rp2040.hwrand32() % 11) - 5);
      int16_t dy = (int16_t)((rp2040.hwrand32() % 9) - 4);
      display.drawPixel((int16_t)ship.x + dx, (int16_t)ship.y + dy, SH110X_WHITE);
    }
    return;
  }

  display.drawLine(noseX, noseY, lX, lY, SH110X_WHITE);
  display.drawLine(lX, lY, rX, rY, SH110X_WHITE);
  display.drawLine(rX, rY, noseX, noseY, SH110X_WHITE);
  display.drawLine(lX, lY, legLX, legLY, SH110X_WHITE);
  display.drawLine(rX, rY, legRX, legRY, SH110X_WHITE);

  if (ship.thrusting) {
    int16_t fx = tx(-7.0f, 0), fy = ty(-7.0f, 0);
    display.drawLine(lX, lY, fx, fy, SH110X_WHITE);
    display.drawLine(rX, rY, fx, fy, SH110X_WHITE);
  }
}

void drawHUD() {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.printf("F:%d", (int)(ship.fuel + 0.5f));

  // Vertical speed as bar on right (safe zone marked)
  int16_t meterX = 124;
  display.drawFastVLine(meterX, 8, 24, SH110X_WHITE);
  int vyMark = 20 + (int)(ship.vy * 18.0f);
  if (vyMark < 8) vyMark = 8;
  if (vyMark > 32) vyMark = 32;
  display.fillRect(meterX - 1, vyMark - 1, 3, 3, SH110X_WHITE);
  // Safe threshold tick
  int safeY = 20 + (int)(MAX_LAND_VY * 18.0f);
  display.drawPixel(meterX - 2, safeY, SH110X_WHITE);
  display.drawPixel(meterX + 2, safeY, SH110X_WHITE);
}

void drawFrame() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  if (state == TITLE) {
    display.setTextSize(2);
    display.setCursor(4, 6);
    display.print(F("LANDER"));
    display.setTextSize(1);
    display.setCursor(4, 30);
    display.print(F("A/C rotate  B thrust"));
    display.setCursor(4, 42);
    display.print(F("Land soft on the pad"));
    display.setCursor(4, 54);
    display.printf("Hi %u  press btn", highScore);
    // Tiny lander doodle
    display.drawTriangle(110, 12, 104, 22, 116, 22, SH110X_WHITE);
    display.drawLine(104, 22, 102, 26, SH110X_WHITE);
    display.drawLine(116, 22, 118, 26, SH110X_WHITE);
    display.display();
    return;
  }

  drawTerrain();
  drawLander();
  drawHUD();

  if (state == LANDED) {
    display.fillRect(14, 16, 100, 28, SH110X_BLACK);
    display.drawRect(14, 16, 100, 28, SH110X_WHITE);
    display.setCursor(28, 22);
    display.print(F("TOUCHDOWN!"));
    display.setCursor(22, 34);
    display.printf("Score %u  btn", score);
  } else if (state == CRASHED) {
    display.fillRect(18, 16, 92, 28, SH110X_BLACK);
    display.drawRect(18, 16, 92, 28, SH110X_WHITE);
    display.setCursor(34, 22);
    display.print(F("CRASHED"));
    display.setCursor(22, 34);
    display.print(F("press btn retry"));
  }

  display.display();
}

void updateNeoPixel() {
  unsigned long now = millis();
  if (now - lastPixel < 30) return;
  lastPixel = now;

  if (state == TITLE) {
    hue += 120;
    // dusty moon grey-blue shimmer
    pixel.setPixelColor(0, pixel.gamma32(pixel.ColorHSV(hue, 80, 140)));
    pixel.show();
  } else if (state == LANDED) {
    setNeo(20, 180, 40);
  } else if (state == CRASHED) {
    setNeo(((now / 200) % 2) ? 200 : 40, 20, 0);
  } else if (ship.thrusting) {
    setNeo(255, 120, 20);
  } else {
    // Greener when descending slowly
    uint8_t g = ship.vy < MAX_LAND_VY ? 160 : 40;
    uint8_t r = ship.vy < MAX_LAND_VY ? 40 : 160;
    setNeo(r, g, 30);
  }
}

void setup() {
  Serial.begin(115200);

#if defined(NEOPIXEL_POWER)
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
#endif
  pixel.begin();
  pixel.setBrightness(45);

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);

  delay(250);
  if (!display.begin(0x3C, true)) {
    display.begin(0x3D, true);
  }
  display.setRotation(1);
  display.clearDisplay();
  display.display();
  delay(50);

  aDown = digitalRead(BUTTON_A) == LOW;
  bDown = digitalRead(BUTTON_B) == LOW;
  cDown = digitalRead(BUTTON_C) == LOW;
  inputLockUntil = millis() + 800;

  state = TITLE;
  lastFrame = millis();
  drawFrame();
  Serial.println(F("Moon Lander ready"));
}

void loop() {
  unsigned long now = millis();
  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.05f) dt = 0.05f;
  lastFrame = now;

  handleInput(dt);
  updateFlight(dt);
  drawFrame();
  updateNeoPixel();
}
