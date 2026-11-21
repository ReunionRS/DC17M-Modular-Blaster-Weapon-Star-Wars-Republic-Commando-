/*
 DC-17m cosplay prop: Nano ATmega328P, TM1637, WS2812, DFPlayer,
 detachable grenade module with Hall sensor and hobby servo.
 No projectile launch hardware is controlled here.

 DFPlayer: Nano RX A1 <- player TX; Nano TX A2 -> player RX.
 Servo signal D6, servo 5V via adequate external supply, common GND.
 Hall MAG=A6 (analog module, optional), Hall grenade inserted=D8.
 Hall modules are active LOW.

 Mode is changed by button D4 only: ASSAULT -> SNIPER -> GRENADE.
 No launcher Hall, no TFT.
*/

#include <TM1637Display.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include <Servo.h>

// ---- Forward declarations (before any function that uses these types) ----
struct AmmoState;
enum Mode : uint8_t;
enum GrenadeCycle : uint8_t;

#define FIRE_BUTTON 2
#define MODE_BUTTON 4
#define MAG_LED_PIN 9
#define MAG_LED_COUNT 5
#define FIRE_LED_PIN A3
#define FIRE_LED_COUNT 10
#define DISPLAY_DIO A4
#define DISPLAY_CLK A5
#define HALL_MAG_PIN A6
#define HALL_GRENADE_PIN 8
#define SERVO_PIN 6
#define MP3_RX A2
#define MP3_TX A1

#define USE_HALL_MAGAZINE 1
#define USE_HALL_GRENADE 1
#define DISPLAY_FLIP_180 1
#define DISPLAY_SHIFT_RIGHT 1

#define SND_BLASTER_RELOAD 1
#define SND_BLASTER_SHOT 2
#define SND_GRENADE_SHOT 3
#define SND_NO_AMMO 4
#define SND_SNIPER_RELOAD 5
#define SND_SNIPER_SHOT 6
#define SND_SWITCH 7

// Servo INVERTED: closed=180, opens to 90 (opposite travel).
// If still wrong direction, swap these two values.
const uint8_t SERVO_OPEN_ANGLE = 90;
const uint8_t SERVO_CLOSED_ANGLE = 0;
const unsigned long SERVO_CLOSE_MS = 450;
const unsigned long FIRE_TO_OPEN_MS = 250;
const unsigned long SERVO_OPEN_MS = 450;
const unsigned long SENSOR_DEBOUNCE_MS = 80;
const unsigned long DEBOUNCE_MS = 25;
const unsigned long HOLD_MS = 400;
const unsigned long AUTO_SHOT_MS = 210;
const unsigned long FLASH_MS = 45;
const unsigned long MAG_REFRESH_MS = 100;
const unsigned long SOUND_SPACING_MS = 160;
const unsigned long NO_AMMO_SPACING_MS = 500;

TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);
Adafruit_NeoPixel fireStrip(FIRE_LED_COUNT, FIRE_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel magStrip(MAG_LED_COUNT, MAG_LED_PIN, NEO_GRB + NEO_KHZ800);
SoftwareSerial mp3Serial(MP3_RX, MP3_TX);
Servo grenadeServo;

// ---- Type definitions (BEFORE any function that uses them) ----
enum Mode : uint8_t { ASSAULT, SNIPER, GRENADE };
struct AmmoState { int loaded; int reserve; uint8_t capacity; };
enum GrenadeCycle : uint8_t {
  GR_WAIT_INSERT,
  GR_CLOSING,
  GR_READY,
  GR_FIRED_DELAY,
  GR_OPENING,
  GR_WAIT_REMOVAL
};

// ---- State variables ----
Mode mode = ASSAULT;
AmmoState assault = {60, 240, 60};
AmmoState sniper = {5, 15, 5};
AmmoState grenade = {4, 0, 4};
GrenadeCycle grenadeCycle = GR_WAIT_INSERT;
unsigned long grenadeCycleAt = 0;
int lastServoAngle = -1;

bool fireStable = HIGH;
bool fireLastRaw = HIGH;
bool modeStable = HIGH;
bool modeLastRaw = HIGH;
bool triggerArmed = true;
unsigned long fireChangedAt = 0;
unsigned long modeChangedAt = 0;
unsigned long firePressedAt = 0;
unsigned long lastShotAt = 0;
unsigned long lastBlasterSound = 0;
unsigned long lastNoAmmoSound = 0;
bool flashActive = false;
unsigned long flashStartedAt = 0;
unsigned long lastMagRefresh = 0;

bool magazineInserted = true;
bool hallMagLastRaw = true;
bool magazineWasRemoved = false;
unsigned long hallMagChangedAt = 0;
unsigned long lastMagDebugAt = 0;
const int MAG_HALL_THRESHOLD = 400;

bool grenadePresent = false;
bool grenadeLastRaw = false;
unsigned long grenadeChangedAt = 0;

// ---- Functions ----
void sendMp3Command(uint8_t command, uint16_t arg) {
  uint8_t hi = arg >> 8, lo = arg & 0xFF;
  uint16_t check = (uint16_t)(0 - (0xFF + 0x06 + command + hi + lo));
  uint8_t packet[10] = {0x7E, 0xFF, 0x06, command, 0x00, hi, lo,
                        (uint8_t)(check >> 8), (uint8_t)(check & 0xFF), 0xEF};
  mp3Serial.write(packet, sizeof(packet));
}
void playSound(uint8_t number) { sendMp3Command(0x12, number); }

// Rate-limited "no ammo": prevents servo micro-jitter caused by
// SoftwareSerial blocking interrupts on every empty-magazine press.
void playNoAmmo() {
  unsigned long now = millis();
  if (now - lastNoAmmoSound >= NO_AMMO_SPACING_MS) {
    lastNoAmmoSound = now;
    playSound(SND_NO_AMMO);
  }
}

AmmoState &ammoForMode() {
  if (mode == ASSAULT) return assault;
  if (mode == SNIPER) return sniper;
  return grenade;
}
bool magazineRequired() { return mode != GRENADE; }
int totalRemaining() {
  AmmoState &a = ammoForMode();
  return a.loaded + a.reserve;
}

uint8_t rotateSegments180(uint8_t s) {
  uint8_t out = s & 0x40;
  if (s & 0x01) out |= 0x08;
  if (s & 0x02) out |= 0x10;
  if (s & 0x04) out |= 0x20;
  if (s & 0x08) out |= 0x01;
  if (s & 0x10) out |= 0x02;
  if (s & 0x20) out |= 0x04;
  return out;
}
void updateDisplay() {
  int val = constrain(totalRemaining(), 0, 9999);
  uint8_t shown[4] = {
    display.encodeDigit((val / 1000) % 10),
    display.encodeDigit((val / 100) % 10),
    display.encodeDigit((val / 10) % 10),
    display.encodeDigit(val % 10)
  };
#if DISPLAY_SHIFT_RIGHT
  if (val < 1000) shown[0] = 0;
#endif
#if DISPLAY_FLIP_180
  uint8_t out[4];
  for (uint8_t i = 0; i < 4; ++i) out[i] = rotateSegments180(shown[3-i]);
  display.setSegments(out, 4, 0);
#else
  display.setSegments(shown, 4, 0);
#endif
}
void updateMagazineLEDs() {
  AmmoState &a = ammoForMode();
  int count = 0;
  if (!magazineRequired() || magazineInserted) {
    if (mode == GRENADE) count = grenade.loaded;
    else count = (a.loaded * MAG_LED_COUNT + a.capacity - 1) / a.capacity;
  }
  count = constrain(count, 0, MAG_LED_COUNT);
  for (int i = 0; i < MAG_LED_COUNT; ++i)
    magStrip.setPixelColor(i, i < count ? magStrip.Color(0, 0, 255) : 0);
  magStrip.show();
}
void flashBlue() {
  flashStartedAt = millis();
  flashActive = true;
  const uint8_t brightness = (uint8_t) random(60, 100);
  for (int i = 0; i < FIRE_LED_COUNT; ++i)
    fireStrip.setPixelColor(i, fireStrip.Color(0, 0, brightness));
  fireStrip.show();
}

bool readMagHall() {
#if USE_HALL_MAGAZINE
  return analogRead(HALL_MAG_PIN) < MAG_HALL_THRESHOLD;
#else
  return true;
#endif
}
bool readGrenadeHall() {
#if USE_HALL_GRENADE
  return digitalRead(HALL_GRENADE_PIN) == LOW;
#else
  return false;
#endif
}

void redrawIndicators() {
  updateDisplay();
  updateMagazineLEDs();
}

// Deduplicates servo writes; reduces jitter when called every loop.
void writeServo(uint8_t angle) {
  if (angle != lastServoAngle) {
    lastServoAngle = angle;
    grenadeServo.write(angle);
  }
}

void enterGrenadeCycle(GrenadeCycle next, unsigned long now) {
  if (grenadeCycle == next) return;
  if (next == GR_CLOSING && grenade.loaded <= 0) return;
  grenadeCycle = next;
  grenadeCycleAt = now;
  if (mode != GRENADE) return;      // <-- серва управляется только в режиме гранатомёта
  if (next == GR_CLOSING) writeServo(SERVO_CLOSED_ANGLE);
  if (next == GR_OPENING || next == GR_WAIT_INSERT)
    writeServo(SERVO_OPEN_ANGLE);
}

void updateGrenadeCycle(unsigned long now) {
  bool raw = readGrenadeHall();
  if (raw != grenadeLastRaw) grenadeChangedAt = now;
  if (now - grenadeChangedAt >= SENSOR_DEBOUNCE_MS && raw != grenadePresent) {
    grenadePresent = raw;
    Serial.println(grenadePresent ? F("GRENADE MAGNET PRESENT")
                                   : F("GRENADE MAGNET REMOVED"));
    if (!grenadePresent) {
      if (grenadeCycle == GR_WAIT_REMOVAL || grenadeCycle == GR_OPENING ||
          grenadeCycle == GR_READY || grenadeCycle == GR_CLOSING)
        enterGrenadeCycle(GR_WAIT_INSERT, now);
    }
  }
  grenadeLastRaw = raw;

  if (mode != GRENADE) return;
  switch (grenadeCycle) {
    case GR_WAIT_INSERT:
      if (grenadePresent && grenade.loaded > 0) enterGrenadeCycle(GR_CLOSING, now);
      break;
    case GR_CLOSING:
      if (!grenadePresent) enterGrenadeCycle(GR_WAIT_INSERT, now);
      else if (now - grenadeCycleAt >= SERVO_CLOSE_MS)
        enterGrenadeCycle(GR_READY, now);
      break;
    case GR_READY:
      if (!grenadePresent) enterGrenadeCycle(GR_WAIT_INSERT, now);
      break;
    case GR_FIRED_DELAY:
      if (now - grenadeCycleAt >= FIRE_TO_OPEN_MS)
        enterGrenadeCycle(GR_OPENING, now);
      break;
    case GR_OPENING:
      if (now - grenadeCycleAt >= SERVO_OPEN_MS)
        enterGrenadeCycle(GR_WAIT_REMOVAL, now);
      break;
    case GR_WAIT_REMOVAL:
      if (!grenadePresent) enterGrenadeCycle(GR_WAIT_INSERT, now);
      break;
  }
}

void shoot() {
  AmmoState &a = ammoForMode();
  if (mode == GRENADE) {
    if (!grenadePresent || grenadeCycle != GR_READY || grenade.loaded <= 0) {
      playNoAmmo();
      Serial.println(F("NO READY GRENADE"));
      return;
    }
    --grenade.loaded;
    playSound(SND_GRENADE_SHOT);
    flashBlue();
    enterGrenadeCycle(GR_FIRED_DELAY, millis());
    redrawIndicators();
    Serial.print(F("GRENADES LEFT="));
    Serial.println(grenade.loaded);
    return;
  }
  if (!magazineInserted) {
    Serial.println(F("SHOT BLOCKED: NO MAGAZINE"));
    return;
  }
  if (a.loaded <= 0) {
    Serial.println(F("SHOT BLOCKED: EMPTY MAGAZINE"));
    playNoAmmo();
    return;
  }
  --a.loaded;
  if (mode == ASSAULT) {
    unsigned long now = millis();
    if (lastBlasterSound == 0 || now - lastBlasterSound >= SOUND_SPACING_MS) {
      playSound(SND_BLASTER_SHOT);
      lastBlasterSound = now;
    }
  } else {
    playSound(SND_SNIPER_SHOT);
  }
  flashBlue();
  redrawIndicators();
}

void changeMode() {
  mode = (Mode)(((uint8_t)mode + 1) % 3);
  triggerArmed = (fireStable == HIGH);
  if (mode == GRENADE) {
    // Вход в режим гранатомёта: открыть створку, ждать гранату
    if (grenadeCycle != GR_WAIT_REMOVAL) {
      grenadeCycle = GR_WAIT_INSERT;
      grenadeCycleAt = millis();
    }
    writeServo(SERVO_OPEN_ANGLE);
  } else {
    // Любой другой режим: створка закрыта
    if (grenadeCycle != GR_WAIT_REMOVAL) grenadeCycle = GR_WAIT_INSERT;
    writeServo(SERVO_CLOSED_ANGLE);
  }
  playSound(SND_SWITCH);
  redrawIndicators();
  Serial.print(F("MODE="));
  Serial.println((int)mode);
}

void reloadCurrentMagazine() {
  if (mode == GRENADE) return;
  AmmoState &a = ammoForMode();

  if (a.loaded > 0) {
    Serial.print(F("RELOAD SKIPPED | current magazine still has "));
    Serial.println(a.loaded);
    return;
  }

  if (mode == ASSAULT && assault.reserve == 0) {
    assault.loaded = 60;
    assault.reserve = 240;
    playSound(SND_BLASTER_RELOAD);
    redrawIndicators();
    Serial.println(F("ASSAULT RESET | TOTAL=0300 | MAG=60 | RESERVE=240"));
    return;
  }

  if (a.reserve <= 0) {
    Serial.println(F("RELOAD BLOCKED | no reserve ammunition"));
    return;
  }

  a.loaded = min((int)a.capacity, a.reserve);
  a.reserve -= a.loaded;
  playSound(mode == ASSAULT ? SND_BLASTER_RELOAD : SND_SNIPER_RELOAD);
  redrawIndicators();
  Serial.print(F("RELOADED | loaded="));
  Serial.print(a.loaded);
  Serial.print(F(" reserve="));
  Serial.println(a.reserve);
}

void updateMagazineHall(unsigned long now) {
#if USE_HALL_MAGAZINE
  bool raw = readMagHall();
  if (raw != hallMagLastRaw) hallMagChangedAt = now;
  if (now - hallMagChangedAt >= SENSOR_DEBOUNCE_MS && raw != magazineInserted) {
    magazineInserted = raw;
    triggerArmed = (fireStable == HIGH);
    if (!magazineInserted) {
      magazineWasRemoved = true;
      Serial.println(F("MAGAZINE REMOVED | FIRE LOCKED"));
    } else {
      Serial.println(F("MAGAZINE INSERTED"));
      if (magazineWasRemoved) {
        magazineWasRemoved = false;
        reloadCurrentMagazine();
      }
    }
    redrawIndicators();
  }
  hallMagLastRaw = raw;

  if (now - lastMagDebugAt >= 1000UL) {
    lastMagDebugAt = now;
    Serial.print(F("HALL A6="));
    Serial.print(analogRead(HALL_MAG_PIN));
    Serial.print(F(" | MAG="));
    Serial.print(magazineInserted ? F("IN") : F("OUT"));
    Serial.print(F(" | ASSAULT="));
    Serial.print(assault.loaded);
    Serial.print(F("+"));
    Serial.println(assault.reserve);
  }
#else
  (void)now;
#endif
}

void setup() {
  Serial.begin(115200);
  pinMode(FIRE_BUTTON, INPUT_PULLUP);
  pinMode(MODE_BUTTON, INPUT_PULLUP);
  pinMode(HALL_GRENADE_PIN, INPUT_PULLUP);
  fireStable = fireLastRaw = digitalRead(FIRE_BUTTON);
  modeStable = modeLastRaw = digitalRead(MODE_BUTTON);
  triggerArmed = (fireStable == HIGH);
  display.setBrightness(7);
  fireStrip.begin();
  fireStrip.setBrightness(40);
  fireStrip.clear();
  fireStrip.show();
  magStrip.begin();
  magStrip.setBrightness(40);
  magStrip.clear();
  magStrip.show();
  grenadeServo.attach(SERVO_PIN);
  writeServo(SERVO_CLOSED_ANGLE);
  magazineInserted = hallMagLastRaw = readMagHall();
#if USE_HALL_MAGAZINE
  Serial.print(F("BOOT | HALL A6="));
  Serial.print(analogRead(HALL_MAG_PIN));
  Serial.print(F(" | MAGAZINE="));
  Serial.println(magazineInserted ? F("IN") : F("OUT"));
#endif
  grenadeLastRaw = grenadePresent = readGrenadeHall();
  redrawIndicators();

  mp3Serial.begin(9600);
  delay(1500);
  sendMp3Command(0x06, 22);
  delay(200);
  Serial.println(F("DC-17M READY | D2 fire | D4 mode"));
}

void loop() {
  unsigned long now = millis();
#if !USE_HALL_MAGAZINE
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') reloadCurrentMagazine();
  }
#endif
  updateGrenadeCycle(now);
  updateMagazineHall(now);

  bool modeRaw = digitalRead(MODE_BUTTON);
  if (modeRaw != modeLastRaw) modeChangedAt = now;
  if (now - modeChangedAt >= DEBOUNCE_MS && modeRaw != modeStable) {
    modeStable = modeRaw;
    if (modeStable == LOW) changeMode();
  }
  modeLastRaw = modeRaw;

  bool fireRaw = digitalRead(FIRE_BUTTON);
  if (fireRaw != fireLastRaw) fireChangedAt = now;
  if (now - fireChangedAt >= DEBOUNCE_MS && fireRaw != fireStable) {
    fireStable = fireRaw;
    if (fireStable == LOW) {
      firePressedAt = now;
      lastShotAt = now;
      if (triggerArmed) shoot();
    } else triggerArmed = true;
  }
  fireLastRaw = fireRaw;

  if (mode == ASSAULT && fireStable == LOW && triggerArmed && magazineInserted &&
      assault.loaded > 0 && now - firePressedAt >= HOLD_MS &&
      now - lastShotAt >= AUTO_SHOT_MS) {
    shoot();
    lastShotAt = now;
  }
  if (flashActive && now - flashStartedAt >= FLASH_MS) {
    fireStrip.clear();
    fireStrip.show();
    flashActive = false;
  }
  if (now - lastMagRefresh >= MAG_REFRESH_MS) {
    lastMagRefresh = now;
    magStrip.show();
  }
}