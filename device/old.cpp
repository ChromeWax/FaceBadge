#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_BNO08x.h>
#include <LittleFS.h>
#include <FS.h>
#include <tinf.h>

#define TFT_CS          D0
#define TFT_RST         D1
#define TFT_DC          D2
#define BNO08X_RESET    -1

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

const int YAW_MAX = 20;
const int PITCH_MAX = 10;
const int ANIM_DURATION = 1000;
int lastYaw = 0;
int lastPitch = 0;
int sy = 0;
int sp = 0;

enum class Mode { Tracking, Animating };
Mode mode = Mode::Tracking;
int anchorYaw = 0;
int anchorPitch = 0;
unsigned long animStart = 0;

const float DELTA_THRESHOLD = 3.0f;
const int STILL_DURATION = 150;
float lastRawYaw = 0;
float lastRawPitch = 0;
float deltaYaw = 0;
float deltaPitch = 0;
unsigned long stillStart = 0;
bool resting = false;

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

}

void drawRaw(const char *filename) {
  File f = LittleFS.open(filename, "r");
  if (!f) return;

  uint16_t numColors;
  f.read((uint8_t*)&numColors, 2);
  if (numColors == 0 || numColors > 256) { f.close(); return; }

  uint16_t palette[256];
  f.read((uint8_t*)palette, numColors * 2);

  uint16_t compressedSize;
  f.read((uint8_t*)&compressedSize, 2);

  uint8_t *compressed = (uint8_t*)malloc(compressedSize);
  uint8_t *indices = (uint8_t*)malloc(240 * 320);
  if (!compressed || !indices) {
    if (compressed) free(compressed);
    if (indices) free(indices);
    f.close();
    return;
  }

  f.read(compressed, compressedSize);
  f.close();

  unsigned int destLen = 240 * 320;
  tinf_zlib_uncompress(indices, &destLen, compressed, compressedSize);
  free(compressed);

  tft.startWrite();
  tft.setAddrWindow(0, 0, 240, 320);

  uint16_t line[240];
  for (int y = 0; y < 320; y++) {
    int off = y * 240;
    for (int x = 0; x < 240; x++) {
      line[x] = palette[indices[off + x]];
    }
    tft.writePixels(line, 240);
  }

  tft.endWrite();
  free(indices);
}

void loop() {
  unsigned long now = millis();

  if (bno08x.wasReset()) {
    setReports();
  }

  if (bno08x.getSensorEvent(&sensorValue)) {
    if (sensorValue.sensorId == SH2_GAME_ROTATION_VECTOR) {

      float r = sensorValue.un.gameRotationVector.real;
      float i = sensorValue.un.gameRotationVector.i;
      float j = sensorValue.un.gameRotationVector.j;
      float k = sensorValue.un.gameRotationVector.k;

      float rawYaw   = atan2(2.0f * (r * k + i * j), 1.0f - 2.0f * (j * j + k * k)) * RAD_TO_DEG;
      float rawPitch = asin(2.0f * (r * j - k * i)) * RAD_TO_DEG;

      rawYaw   = constrain(rawYaw,   -YAW_MAX,   YAW_MAX);
      rawPitch = constrain(rawPitch, -PITCH_MAX, PITCH_MAX);

      deltaYaw   = abs(rawYaw   - lastRawYaw);
      deltaPitch = abs(rawPitch - lastRawPitch);
      lastRawYaw   = rawYaw;
      lastRawPitch = rawPitch;

      if (deltaYaw >= DELTA_THRESHOLD || deltaPitch >= DELTA_THRESHOLD) {
        stillStart = 0;
        resting = false;
      }

      if (mode == Mode::Tracking) {
        if (!resting) {
          sy = round(rawYaw / 2.0f) * 2;
          sp = round(rawPitch / 2.0f) * 2;

          if (deltaYaw < DELTA_THRESHOLD && deltaPitch < DELTA_THRESHOLD) {
            if (stillStart == 0) {
              stillStart = now;
            }
            if (now - stillStart > STILL_DURATION && !(lastYaw == 0 && lastPitch == 0)) {
              anchorYaw = lastYaw;
              anchorPitch = lastPitch;
              animStart = now;
              mode = Mode::Animating;
              stillStart = 0;
            }
          }
        }
      }
    }
  } else if (mode != Mode::Animating) {
    return;
  }

  if (mode == Mode::Animating) {
    if (deltaYaw > DELTA_THRESHOLD || deltaPitch > DELTA_THRESHOLD) {
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
        resting = true;
      }
    }
  }

  if (mode != Mode::Animating && sy == lastYaw && sp == lastPitch) return;
  lastYaw = sy;
  lastPitch = sp;

  char filename[64];
  snprintf(filename, sizeof(filename), "/pitch_%+03d_yaw_%+03d.raw", sp, sy);

  drawRaw(filename);
}
