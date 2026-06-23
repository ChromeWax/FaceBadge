#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_BNO08x.h>
#include <LittleFS.h>

#define TFT_CS          D0
#define TFT_RST         D1
#define TFT_DC          D2
#define BNO08X_RESET    -1

const uint32_t TARGET_FPS = 24;
const uint32_t FRAME_INTERVAL_US = 1000000 / TARGET_FPS;
const float DEADZONE = 0.005f;

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

void setReports() {
    if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR)) {
        Serial.println("Could not enable game vector");
    }
}

void setup() {
    Serial.begin(115200);

    if (!bno08x.begin_I2C()) {
        Serial.println("Failed to find BNO08x chip");
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
        Serial.println("(None, None)");
        return;
    }

    if (sv.sensorId == SH2_GAME_ROTATION_VECTOR) {
        float j = sv.un.gameRotationVector.j;
        float k = sv.un.gameRotationVector.k;

        String yawDir = "None";
        String pitchDir = "None";

        if (hasPrev) {
            float dk = k - prevK;
            float dj = j - prevJ;

            if (fabs(dk) > DEADZONE) {
                yawDir = (dk > 0) ? "Right" : "Left";
            }

            if (fabs(dj) > DEADZONE) {
                pitchDir = (dj > 0) ? "Down" : "Up";
            }
        }

        prevJ = j;
        prevK = k;
        hasPrev = true;

        Serial.print("(");
        Serial.print(yawDir);
        Serial.print(", ");
        Serial.print(pitchDir);
        Serial.println(")");
    }
}
