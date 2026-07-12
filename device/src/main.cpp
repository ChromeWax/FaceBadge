#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_BNO08x.h>
#include <LittleFS.h>
#include <FS.h>
#include <tinf.h>

#define TFT_CS          D2
#define TFT_RST         D1
#define TFT_DC          D0
#define BNO08X_RESET    -1

const uint32_t TARGET_FPS = 12;
const uint32_t FRAME_INTERVAL_US = 1000000 / TARGET_FPS;
const float ANGLE_DEADZONE = 1.0f;
const int YAW_MAX = 20;
const int PITCH_MAX = 10;
const int STEP = 4;
const int RETURN_DURATION_MS = 500;

enum class YawDir : int8_t { None, Left, Right };
enum class PitchDir : int8_t { None, Down, Up };

int sy = 0;
int sp = 0;
int lastYaw = 0;
int lastPitch = 0;

bool isAnimating = false;
unsigned long animStartMs = 0;
int animOriginSy = 0;
int animOriginSp = 0;

static float prevYaw = 0.0f;
static float prevPitch = 0.0f;
static bool hasPrev = false;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_BNO08x bno08x(BNO08X_RESET);

uint32_t lastFrameUs = 0;

void waitForNextFrame() {
    uint32_t now = micros();
    uint32_t elapsed = now - lastFrameUs;
    if (elapsed < FRAME_INTERVAL_US) {
        delayMicroseconds(FRAME_INTERVAL_US - elapsed);
    }
    lastFrameUs = micros();
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

void quaternionToEuler(float qr, float qi, float qj, float qk,
                       float* yaw, float* pitch, float* roll, bool degrees) {
    float sqr = qr * qr;
    float sqi = qi * qi;
    float sqj = qj * qj;
    float sqk = qk * qk;

    *yaw   = atan2(2.0f * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
    *pitch = asin(-2.0f * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
    *roll  = atan2(2.0f * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

    if (degrees) {
        *yaw   *= RAD_TO_DEG;
        *pitch *= RAD_TO_DEG;
        *roll  *= RAD_TO_DEG;
    }
}

void remapMounting(float sensorYaw, float sensorPitch, float sensorRoll,
                   float* badgeYaw, float* badgePitch) {
    *badgeYaw = sensorYaw;
    *badgePitch = sensorRoll;
}

void setReports() {
    if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR)) {
        Serial.println("Could not enable game vector");
    }
}

void setup() {
    Serial.begin(115200);

    tft.init(240, 320);
    tft.setRotation(2);
    tft.setSPISpeed(80000000);
    tft.fillScreen(ST77XX_BLACK);

    Wire.begin();
    while (!bno08x.begin_I2C()) {
        Serial.println("Waiting for BNO08x chip...");
        tft.println("Waiting for BNO08x chip...");
        delay(500);
    }

    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed");
        tft.println("FS mount failed!");
        delay(500);
    }

    setReports();
}

void loop() {
    waitForNextFrame();

    if (bno08x.wasReset()) {
        setReports();
        hasPrev = false;
    }

    sh2_SensorValue_t sv;
    bool gotEvent = false;
    while (bno08x.getSensorEvent(&sv)) {
        gotEvent = true;
    }

    YawDir yawDir = YawDir::None;
    PitchDir pitchDir = PitchDir::None;

    if (gotEvent && sv.sensorId == SH2_GAME_ROTATION_VECTOR) {
        float real = sv.un.gameRotationVector.real;
        float i = sv.un.gameRotationVector.i;
        float j = sv.un.gameRotationVector.j;
        float k = sv.un.gameRotationVector.k;

        float sensorYaw, sensorPitch, sensorRoll;
        quaternionToEuler(real, i, j, k, &sensorYaw, &sensorPitch, &sensorRoll, true);

        float badgeYaw, badgePitch;
        remapMounting(sensorYaw, sensorPitch, sensorRoll, &badgeYaw, &badgePitch);

        Serial.printf("s(y=%.1f p=%.1f r=%.1f) b(y=%.1f p=%.1f)\n",
                      sensorYaw, sensorPitch, sensorRoll, badgeYaw, badgePitch);

        if (hasPrev) {
            float dy = badgeYaw - prevYaw;
            float dp = badgePitch - prevPitch;

            if (dy > 180.0f) dy -= 360.0f;
            if (dy < -180.0f) dy += 360.0f;
            if (dp > 180.0f) dp -= 360.0f;
            if (dp < -180.0f) dp += 360.0f;

            if (fabs(dy) > ANGLE_DEADZONE) {
                yawDir = (dy > 0) ? YawDir::Right : YawDir::Left;
            }
            if (fabs(dp) > ANGLE_DEADZONE) {
                pitchDir = (dp > 0) ? PitchDir::Up : PitchDir::Down;
            }
        }

        prevYaw = badgeYaw;
        prevPitch = badgePitch;
        hasPrev = true;
    }

    if (yawDir != YawDir::None || pitchDir != PitchDir::None) {
        isAnimating = false;
        if (yawDir == YawDir::Right) sy = constrain(sy + STEP, -YAW_MAX, YAW_MAX);
        if (yawDir == YawDir::Left)  sy = constrain(sy - STEP, -YAW_MAX, YAW_MAX);
        if (pitchDir == PitchDir::Down) sp = constrain(sp + STEP, -PITCH_MAX, PITCH_MAX);
        if (pitchDir == PitchDir::Up)   sp = constrain(sp - STEP, -PITCH_MAX, PITCH_MAX);
    } else if (sy != 0 || sp != 0) {
        if (!isAnimating) {
            isAnimating = true;
            animStartMs = millis();
            animOriginSy = sy;
            animOriginSp = sp;
        }

        float t = (float)(millis() - animStartMs) / RETURN_DURATION_MS;
        if (t >= 1.0f) {
            sy = 0;
            sp = 0;
            isAnimating = false;
        } else {
            float ease = 1.0f - pow(1.0f - t, 3.0f);
            sy = round(animOriginSy * (1.0f - ease) / STEP) * STEP;
            sp = round(animOriginSp * (1.0f - ease) / STEP) * STEP;
        }
    }

    if (sy == lastYaw && sp == lastPitch) return;
    lastYaw = sy;
    lastPitch = sp;

    char filename[64];
    snprintf(filename, sizeof(filename), "/pitch_%+03d_yaw_%+03d.raw", sp, sy);

    drawRaw(filename);
}
