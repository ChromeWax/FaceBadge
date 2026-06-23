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

const uint32_t TARGET_FPS = 24;
const uint32_t FRAME_INTERVAL_US = 1000000 / TARGET_FPS;
const float DEADZONE = 0.005f;
const int YAW_MAX = 20;
const int PITCH_MAX = 10;
const int STEP = 2;

enum class YawDir : int8_t { None, Left, Right };
enum class PitchDir : int8_t { None, Down, Up };

YawDir yawDirection = YawDir::None;
PitchDir pitchDirection = PitchDir::None;

int sy = 0;
int sp = 0;
int lastYaw = 0;
int lastPitch = 0;

static float prevJ = 0.0f;
static float prevK = 0.0f;
static bool hasPrev = false;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

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

void setReports() {
    if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR)) {
        Serial.println("Could not enable game vector");
    }
}

void setup() {
    Serial.begin(115200);

    tft.init(240, 320);
    tft.setRotation(0);
    tft.setSPISpeed(80000000);
    tft.fillScreen(ST77XX_BLACK);

    Wire.begin();
    if (!bno08x.begin_I2C()) {
        Serial.println("Failed to find BNO08x chip");
        tft.println("BNO08x not found!");
        while (1) delay(10);
    }

    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed");
        tft.println("FS mount failed!");
        while (1) delay(10);
    }

    setReports();
}

void loop() {
    waitForNextFrame();

    if (bno08x.wasReset()) {
        setReports();
    }

    sh2_SensorValue_t sv;
    bool gotEvent = false;
    while (bno08x.getSensorEvent(&sv)) {
        gotEvent = true;
    }

    if (!gotEvent) {
        yawDirection = YawDir::None;
        pitchDirection = PitchDir::None;
    } else if (sv.sensorId == SH2_GAME_ROTATION_VECTOR) {
        float j = sv.un.gameRotationVector.j;
        float k = sv.un.gameRotationVector.k;

        yawDirection = YawDir::None;
        pitchDirection = PitchDir::None;

        if (hasPrev) {
            float dk = k - prevK;
            float dj = j - prevJ;

            if (fabs(dk) > DEADZONE) {
                yawDirection = (dk > 0) ? YawDir::Right : YawDir::Left;
            }

            if (fabs(dj) > DEADZONE) {
                pitchDirection = (dj > 0) ? PitchDir::Down : PitchDir::Up;
            }
        }

        prevJ = j;
        prevK = k;
        hasPrev = true;
    }

    if (yawDirection == YawDir::Right) sy = constrain(sy + STEP, -YAW_MAX, YAW_MAX);
    if (yawDirection == YawDir::Left)  sy = constrain(sy - STEP, -YAW_MAX, YAW_MAX);
    if (pitchDirection == PitchDir::Down) sp = constrain(sp + STEP, -PITCH_MAX, PITCH_MAX);
    if (pitchDirection == PitchDir::Up)   sp = constrain(sp - STEP, -PITCH_MAX, PITCH_MAX);

    if (sy == lastYaw && sp == lastPitch) return;
    lastYaw = sy;
    lastPitch = sp;

    char filename[64];
    snprintf(filename, sizeof(filename), "/pitch_%+03d_yaw_%+03d.raw", sp, sy);

    drawRaw(filename);
}
