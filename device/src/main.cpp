#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_BNO08x.h>
#include <TJpg_Decoder.h>
#include <LittleFS.h>

#define TFT_CS          D0
#define TFT_RST         D1
#define TFT_DC          D2
#define BNO08X_RESET    -1

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

const int YAW_MAX = 20;
const int PITCH_MAX = 10;
const int ANIM_DURATION = 400;
int lastYaw = 0;
int lastPitch = 0;

enum class Mode { Tracking, Animating };
Mode mode = Mode::Tracking;
int anchorYaw = 0;
int anchorPitch = 0;
unsigned long animStart = 0;

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *data) {
  tft.startWrite();
  tft.setAddrWindow(x, y, w, h);
  tft.writePixels(data, w * h);
  tft.endWrite();
  return true;
}

void setReports() {
  bno08x.enableReport(SH2_GAME_ROTATION_VECTOR, 20000);
}

void setup() {
  Serial.begin(115200);

  tft.init(240, 320);
  tft.setRotation(0);
  tft.setSPISpeed(80000000);
  tft.fillScreen(ST77XX_BLACK);

  Wire.begin();
  if (!bno08x.begin_I2C()) {
    Serial.println("BNO08x not found");
    tft.println("BNO08x not found!");
    while (1) delay(10);
  }
  setReports();

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    tft.println("FS mount failed!");
    while (1) delay(10);
  }

  TJpgDec.setCallback(tft_output);
}

void loop() {
  unsigned long now = millis();

  if (bno08x.wasReset()) {
    setReports();
  }

  if (!bno08x.getSensorEvent(&sensorValue)) {
    return;
  }

  if (sensorValue.sensorId != SH2_GAME_ROTATION_VECTOR) return;

  float r = sensorValue.un.gameRotationVector.real;
  float i = sensorValue.un.gameRotationVector.i;
  float j = sensorValue.un.gameRotationVector.j;
  float k = sensorValue.un.gameRotationVector.k;

  float rawYaw   = atan2(2.0f * (r * k + i * j), 1.0f - 2.0f * (j * j + k * k)) * RAD_TO_DEG;
  float rawPitch = asin(2.0f * (r * j - k * i)) * RAD_TO_DEG;

  rawYaw   = constrain(rawYaw,   -YAW_MAX,   YAW_MAX);
  rawPitch = constrain(rawPitch, -PITCH_MAX, PITCH_MAX);

  int sy = round(rawYaw   / 2.0f) * 2;
  int sp = round(rawPitch / 2.0f) * 2;

  if (mode == Mode::Tracking) {
    if (sy == 0 && sp == 0 && !(lastYaw == 0 && lastPitch == 0)) {
      anchorYaw = lastYaw;
      anchorPitch = lastPitch;
      animStart = now;
      mode = Mode::Animating;
      sy = anchorYaw;
      sp = anchorPitch;
    }
  }

  if (mode == Mode::Animating) {
    if (abs(sy) > 2 || abs(sp) > 2) {
      mode = Mode::Tracking;
    } else {
      float t = min((float)(now - animStart) / ANIM_DURATION, 1.0f);
      float ease = 1.0f - pow(1.0f - t, 3.0f);
      sy = round(anchorYaw * (1.0f - ease) / 2.0f) * 2;
      sp = round(anchorPitch * (1.0f - ease) / 2.0f) * 2;
      if (t >= 1.0f) {
        sy = 0;
        sp = 0;
        mode = Mode::Tracking;
      }
    }
  }

  if (sy == lastYaw && sp == lastPitch) return;
  lastYaw = sy;
  lastPitch = sp;

  char filename[64];
  snprintf(filename, sizeof(filename), "/pitch_%+03d_yaw_%+03d.jpg", sp, sy);

  TJpgDec.drawFsJpg(0, 0, filename, LittleFS);
}
