// 1-bit Adafruit_GFX-compatible framebuffer for PicoDVI.

#include <PicoDVI.h>

//DVIGFX1 display(640, 480, dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_30, pimoroni_demo_hdmi_cfg);
DVIGFX1 display(800, 480, dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_30, pimoroni_demo_hdmi_cfg);

void setup() {
  Serial.begin(115200);
  //while(!Serial);
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;) digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
}

void loop() {
  // Draw random lines
  display.drawLine(random(display.width()), random(display.height()), random(display.width()), random(display.height()), random(2));
}
