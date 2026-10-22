#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

// Guition JC3248W535C: AXS15231B, QSPI screen, 320x480 portrait.
static constexpr int BACKLIGHT = 1;
static constexpr uint8_t TOUCH_ADDR = 0x3B;
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
static Arduino_GFX *panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
static Arduino_Canvas *gfx = new Arduino_Canvas(320, 480, panel, 0, 0, 0);
static bool displayReady = false;

static void logStatus(const char *line) {
  Serial.println(line);
  Serial.flush();
}

static bool readTouch(int &x, int &y) {
  static const uint8_t cmd[8] = {0xB5,0xAB,0xA5,0x5A,0,0,0,8};
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(cmd, sizeof(cmd));
  if (Wire.endTransmission() != 0 || Wire.requestFrom(TOUCH_ADDR, (uint8_t)8) != 8) return false;
  uint8_t data[8];
  for (auto &v : data) v = Wire.read();
  if (!data[1] || data[1] > 4) return false;
  x = ((data[2] & 15) << 8) | data[3];
  y = ((data[4] & 15) << 8) | data[5];
  return true;
}

void setup() {
  pinMode(BACKLIGHT, OUTPUT);
  digitalWrite(BACKLIGHT, LOW);
  Serial.begin(115200);
  delay(1200);
  logStatus("DIAG V2: BOOT OK");
  Serial.printf("DIAG V2: heap=%u PSRAM=%u flash=%u\n", ESP.getFreeHeap(), ESP.getPsramSize(), ESP.getFlashChipSize());
  Serial.flush();
  Wire.begin(4, 8);
  Wire.setClock(400000);
  logStatus("DIAG V2: TOUCH BUS INIT");
  digitalWrite(BACKLIGHT, HIGH);
  logStatus("DIAG V2: BACKLIGHT HIGH");
  logStatus("DIAG V2: DISPLAY INIT START");
  displayReady = gfx->begin(40000000);
  if (!displayReady) {
    logStatus("DIAG V2: DISPLAY INIT FAILED");
    return;
  }
  logStatus("DIAG V2: DISPLAY INIT OK");
  gfx->fillScreen(0x07E0); // green, RGB565
  gfx->setTextColor(0x0000);
  gfx->setTextSize(3);
  gfx->setCursor(34, 170);
  gfx->println("DISPLAY OK");
  gfx->setTextSize(2);
  gfx->setCursor(36, 220);
  gfx->println("JC3248W535C");
  gfx->setCursor(36, 250);
  gfx->println("TOUCH TEST");
  gfx->flush();
  logStatus("DIAG V2: FRAME FLUSHED");
}

void loop() {
  static uint32_t lastTouch = 0, lastBeat = 0;
  if (millis() - lastBeat > 5000) {
    lastBeat = millis();
    Serial.printf("DIAG V2: ALIVE millis=%lu display=%d\n", (unsigned long)millis(), (int)displayReady);
  }
  int x, y;
  if (readTouch(x, y) && millis() - lastTouch > 250) {
    lastTouch = millis();
    Serial.printf("DIAG V2: TOUCH x=%d y=%d\n", x, y);
    if (displayReady) {
      gfx->fillRect(20, 315, 280, 52, 0x07E0);
      gfx->setCursor(25, 325);
      gfx->setTextColor(0x0000);
      gfx->setTextSize(2);
      gfx->printf("X:%d  Y:%d", x, y);
      gfx->flush();
    }
  }
  delay(20);
}
