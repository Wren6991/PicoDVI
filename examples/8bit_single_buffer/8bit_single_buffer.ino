// 8-bit Adafruit_GFX-compatible framebuffer for PicoDVI.

#include <PicoDVI.h>

// 8-bit currently supports 320x240 and 400x240 resolutions only.
DVIGFX8 display(DVI_RES_400x240p60, pimoroni_demo_hdmi_cfg);

void setup() {
  Serial.begin(115200);
  //while(!Serial);
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;) digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }

  // Randomize color palette. First entry is left black, last is set white.
  for (int i=1; i<255; i++) display.setColor(i, random(65536));
  display.setColor(255, 0xFFFF);
}

void loop() {
  // Draw random lines
  display.drawLine(random(display.width()), random(display.height()), random(display.width()), random(display.height()), random(256));
}
