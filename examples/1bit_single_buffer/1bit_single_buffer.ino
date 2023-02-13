// 1-bit Adafruit_GFX-compatible framebuffer for PicoDVI.

#include <PicoDVI.h>

// 1-bit currently supports 640x480 and 800x480 resolutions only.
DVIGFX1 display(DVI_RES_800x480p60, false, pimoroni_demo_hdmi_cfg);

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
