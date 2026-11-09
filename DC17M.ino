/*
 DC-17m cosplay prop: Nano ATmega328P, TM1637, WS2812, DFPlayer,
 detachable grenade module with ST7735S TFT, 2 additional Hall modules,
 and hobby servo. No projectile launch hardware is controlled here.

 DFPlayer: Nano RX A1 <- player TX; Nano TX A2 -> player RX.
 TFT ST7735S 160x80 SPI: SCL=D13, SDA=D11, RES=D7, DC=D8, CS=D10.
      TFT power/logic: verify your display's rated voltage; many bare
      0.96-inch panels are 3.3 V only and need 3.3 V logic conversion.
 Servo signal D9, servo 5V via adequate external supply, common GND.
 Hall MAG=A6 (analog module, optional), module attached=D4,
 Hall grenade inserted=D6. Hall modules are active LOW.

 To test without any Hall modules, set USE_HALL_MAGAZINE 0,
 USE_HALL_LAUNCHER 0, USE_HALL_GRENADE 0, but grenade firing will be
 disabled until proper sensing is enabled (safe by default).
*/

#include <TM1637Display.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include <Servo.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#define FIRE_BUTTON 2
#define MODE_BUTTON 5
#define MAG_LED_PIN 3
#define MAG_LED_COUNT 5
#define FIRE_LED_PIN A3
#define FIRE_LED_COUNT 10
#define DISPLAY_DIO A4
#define DISPLAY_CLK A5
#define HALL_MAG_PIN A6
#define HALL_MODULE_PIN 4
#define HALL_GRENADE_PIN 6
#define SERVO_PIN 9
#define TFT_RST 7
#define TFT_DC 8
#define TFT_CS 10
#define MP3_RX A1
#define MP3_TX A2

#define USE_HALL_MAGAZINE 1
#define USE_HALL_LAUNCHER 1
#define USE_HALL_GRENADE 1
#define USE_TFT 1
#define DISPLAY_FLIP_180 1
#define DISPLAY_SHIFT_RIGHT 1
#define TFT_ROTATION 1  // 1 or 3: landscape, 0 or 2: portrait

#define SND_BLASTER_RELOAD 1
#define SND_BLASTER_SHOT 2
#define SND_GRENADE_SHOT 3
#define SND_NO_AMMO 4
#define SND_SNIPER_RELOAD 5
#define SND_SNIPER_SHOT 6
#define SND_SWITCH 7

// Adjust angles to the linkage on your unloaded prop (no force/obstruction).
const uint8_t SERVO_OPEN_ANGLE = 15;
const uint8_t SERVO_CLOSED_ANGLE = 95;
const unsigned long SERVO_CLOSE_MS = 450;
const unsigned long FIRE_TO_OPEN_MS = 250;
const unsigned long SERVO_OPEN_MS = 450;
const unsigned long SENSOR_DEBOUNCE_MS = 80;
const unsigned long DEBOUNCE_MS = 25;
const unsigned long HOLD_MS = 400;
const unsigned long AUTO_SHOT_MS = 100;
const unsigned long FLASH_MS = 45;
const unsigned long MAG_REFRESH_MS = 100;
const unsigned long SOUND_SPACING_MS = 160;

TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);
Adafruit_NeoPixel fireStrip(FIRE_LED_COUNT, FIRE_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel magStrip(MAG_LED_COUNT, MAG_LED_PIN, NEO_GRB + NEO_KHZ800);
SoftwareSerial mp3Serial(MP3_RX, MP3_TX);
Servo grenadeServo;
#if USE_TFT
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);  // SPI MOSI D11, SCK D13
bool tftReady = false;
#endif

enum Mode : uint8_t { ASSAULT, SNIPER, GRENADE };
Mode mode = ASSAULT;
Mode modeBeforeLauncher = ASSAULT;
struct AmmoState { int loaded; int reserve; uint8_t capacity; };
AmmoState assault = {60, 240, 60};
AmmoState sniper = {5, 15, 5};
AmmoState grenade = {4, 0, 4};

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
bool flashActive = false;
unsigned long flashStartedAt = 0;
unsigned long lastMagRefresh = 0;

bool magazineInserted = true;
bool hallMagLastRaw = true;
bool magazineWasRemoved = false;
unsigned long hallMagChangedAt = 0;
unsigned long lastMagDebugAt = 0;
// Hall sensor A6: active-LOW (magnet nearby = ADC below threshold)
const int MAG_HALL_THRESHOLD = 400;

bool launcherAttached = false;
bool moduleLastRaw = false;
unsigned long moduleChangedAt = 0;
bool grenadePresent = false;
bool grenadeLastRaw = false;
unsigned long grenadeChangedAt = 0;

enum GrenadeCycle : uint8_t {
  GR_WAIT_INSERT,
  GR_CLOSING,
  GR_READY,
  GR_FIRED_DELAY,
  GR_OPENING,
  GR_WAIT_REMOVAL
};
GrenadeCycle grenadeCycle = GR_WAIT_INSERT;
unsigned long grenadeCycleAt = 0;

void sendMp3Command(uint8_t command, uint16_t arg) {
  uint8_t hi = arg >> 8, lo = arg & 0xFF;
  uint16_t check = (uint16_t)(0 - (0xFF + 0x06 + command + hi + lo));
  uint8_t packet[10] = {0x7E, 0xFF, 0x06, command, 0x00, hi, lo,
                        (uint8_t)(check >> 8), (uint8_t)(check & 0xFF), 0xEF};
  mp3Serial.write(packet, sizeof(packet));
}
void playSound(uint8_t number) { sendMp3Command(0x12, number); }

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
bool readLauncherHall() {
#if USE_HALL_LAUNCHER
  return digitalRead(HALL_MODULE_PIN) == LOW;
#else
  return false;
#endif
}
bool readGrenadeHall() {
#if USE_HALL_GRENADE
  return digitalRead(HALL_GRENADE_PIN) == LOW;
#else
  return false;
#endif
}

// Draw only at changes; default 5x7 font does not need framebuffer RAM.
void drawTft() {
#if USE_TFT
  if (!launcherAttached || !tftReady) return;
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 4);
  tft.print(F("GRENADE"));
  tft.setTextColor(grenade.loaded ? ST77XX_GREEN : ST77XX_RED);
  tft.setTextSize(5);
  tft.setCursor(39, 23);
  tft.print('0');
  tft.print(grenade.loaded);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(17, 70);
  if (grenade.loaded == 0) tft.print(F("EMPTY"));
  else if (grenadeCycle == GR_READY) tft.print(F("READY TO FIRE"));
  else if (grenadeCycle == GR_CLOSING) tft.print(F("LOCKING..."));
  else if (grenadeCycle == GR_FIRED_DELAY || grenadeCycle == GR_OPENING)
    tft.print(F("FIRED / OPENING"));
  else if (grenadeCycle == GR_WAIT_REMOVAL) tft.print(F("REMOVE GRENADE"));
  else tft.print(F("INSERT GRENADE"));
#endif
}
void redrawIndicators() {
  updateDisplay();
  updateMagazineLEDs();
  drawTft();
}

void enterGrenadeCycle(GrenadeCycle next, unsigned long now) {
  if (grenadeCycle == next) return;
  grenadeCycle = next;
  grenadeCycleAt = now;
  if (next == GR_CLOSING) grenadeServo.write(SERVO_CLOSED_ANGLE);
  if (next == GR_OPENING || next == GR_WAIT_INSERT)
    grenadeServo.write(SERVO_OPEN_ANGLE);
  drawTft();
}

void setLauncherAttached(bool present, unsigned long now) {
  if (launcherAttached == present) return;
  launcherAttached = present;
  triggerArmed = (fireStable == HIGH);
  if (present) {
    if (mode != GRENADE) modeBeforeLauncher = mode;
    mode = GRENADE;
    // If an already-spent grenade remains in place, do not auto-rearm.
    if (grenadeCycle == GR_WAIT_REMOVAL && grenadePresent)
      grenadeServo.write(SERVO_OPEN_ANGLE);
    else {
      grenadeCycle = GR_WAIT_INSERT;
      grenadeServo.write(SERVO_OPEN_ANGLE);
    }
#if USE_TFT
    tft.initR(INITR_MINI160x80);
    tft.setRotation(TFT_ROTATION);
    tftReady = true;
#endif
    Serial.println(F("LAUNCHER ATTACHED"));
  } else {
    grenadeServo.write(SERVO_OPEN_ANGLE);
#if USE_TFT
    tftReady = false;
#endif
    mode = modeBeforeLauncher;
    // A spent grenade must be removed before another may be armed.
    if (grenadeCycle != GR_WAIT_REMOVAL) grenadeCycle = GR_WAIT_INSERT;
    Serial.println(F("LAUNCHER REMOVED"));
  }
  redrawIndicators();
  (void)now;
}

void updateGrenadeCycle(unsigned long now) {
  bool raw = readGrenadeHall();
  if (raw != grenadeLastRaw) grenadeChangedAt = now;
  if (now - grenadeChangedAt >= SENSOR_DEBOUNCE_MS && raw != grenadePresent) {
    grenadePresent = raw;
    Serial.println(grenadePresent ? F("GRENADE MAGNET PRESENT") : F("GRENADE MAGNET REMOVED"));
    if (!grenadePresent) {
      if (grenadeCycle == GR_WAIT_REMOVAL || grenadeCycle == GR_OPENING ||
          grenadeCycle == GR_READY || grenadeCycle == GR_CLOSING)
        enterGrenadeCycle(GR_WAIT_INSERT, now);
    }
  }
  grenadeLastRaw = raw;

  if (!launcherAttached || mode != GRENADE) return;
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
    // Prop-only action, no launch actuator, max one shot per insertion.
    if (!launcherAttached || !grenadePresent || grenadeCycle != GR_READY ||
        grenade.loaded <= 0) {
      playSound(SND_NO_AMMO);
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
    // No muzzle flash and no ammo decrement.
    return;
  }
  if (a.loaded <= 0) {
    Serial.println(F("SHOT BLOCKED: EMPTY MAGAZINE"));
    playSound(SND_NO_AMMO);
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
  // Launcher physically attached always overrides D5.
  if (launcherAttached) return;
  mode = (Mode)(((uint8_t)mode + 1) % 3);
  triggerArmed = (fireStable == HIGH);
  playSound(SND_SWITCH);
  redrawIndicators();
}

// Called after an actual Hall-confirmed removal AND reinsertion.
// Assault: 60 per magazine, 300 per cycle. At complete exhaustion (0000),
// inserting another magazine starts a fresh 300-round cycle automatically.
// Sniper still uses only its remaining 20-round supply.
void reloadCurrentMagazine() {
  if (mode == GRENADE) return;
  AmmoState &a = ammoForMode();

  // Prevent filling ammunition just by pulling out a partly used magazine.
  if (a.loaded > 0) {
    Serial.print(F("RELOAD SKIPPED | current magazine still has "));
    Serial.println(a.loaded);
    return;
  }

  // New requirement: when ALL 300 assault rounds have been consumed,
  // the next physical magazine swap resets the whole assault supply.
  if (mode == ASSAULT && assault.reserve == 0) {
    assault.loaded = 60;    // New first magazine
    assault.reserve = 240;  // Four further magazines
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

// A6 is an analog-only pin; INPUT_PULLUP is unavailable on A6.
// 3-pin Hall module must provide a pull-up and stable 0/5V signal on S.
void updateMagazineHall(unsigned long now) {
#if USE_HALL_MAGAZINE
  bool raw = readMagHall();
  if (raw != hallMagLastRaw) hallMagChangedAt = now;
  if (now - hallMagChangedAt >= SENSOR_DEBOUNCE_MS && raw != magazineInserted) {
    magazineInserted = raw;
    // Require trigger release after inserting/removing magazine.
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

  // Voltage diagnostic: remove/reinsert magazine and watch A6 change.
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

void updateLauncherHall(unsigned long now) {
  bool raw = readLauncherHall();
  if (raw != moduleLastRaw) moduleChangedAt = now;
  if (now - moduleChangedAt >= SENSOR_DEBOUNCE_MS && raw != launcherAttached)
    setLauncherAttached(raw, now);
  moduleLastRaw = raw;
}

void setup() {
  Serial.begin(115200);
  pinMode(FIRE_BUTTON, INPUT_PULLUP);
  pinMode(MODE_BUTTON, INPUT_PULLUP);
  pinMode(HALL_MODULE_PIN, INPUT_PULLUP);
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
  grenadeServo.write(SERVO_OPEN_ANGLE);
  magazineInserted = hallMagLastRaw = readMagHall();
#if USE_HALL_MAGAZINE
  Serial.print(F("BOOT | HALL A6="));
  Serial.print(analogRead(HALL_MAG_PIN));
  Serial.print(F(" | MAGAZINE="));
  Serial.println(magazineInserted ? F("IN") : F("OUT"));
#endif
  moduleLastRaw = launcherAttached = readLauncherHall();
  grenadeLastRaw = grenadePresent = readGrenadeHall();
  redrawIndicators();

  mp3Serial.begin(9600);
  delay(1500);
  sendMp3Command(0x06, 22);
  delay(200);
  if (launcherAttached) {
    // Start module after MCU and external 3.3-V supply are stable.
    launcherAttached = false;
    setLauncherAttached(true, millis());
  }
  Serial.println(F("DC-17M READY | D2 fire | D5 mode | TFT ST7735S"));
}

void loop() {
  unsigned long now = millis();
#if !USE_HALL_MAGAZINE
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') reloadCurrentMagazine();
  }
#endif
  updateLauncherHall(now);
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
