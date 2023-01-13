// PicoDVI-based "virtual SPITFT" display. Receives graphics commands/data
// over 4-wire SPI interface, mimicking functionality of displays such as
// ILI9341, but shown on monitor instead.

// THIS IS A PLACEHOLDER AND NOT ACTUALLY WRITTEN YET.

#include <PicoDVI.h> // Core display & graphics library

// Your basic 320x240 16-bit color display:
DVIGFX16 display(320, 240, dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_20, pimoroni_demo_hdmi_cfg);
// Not all RP2040s can deal with the 295 MHz overclock this requires, but if you'd like to try:
//DVIGFX16 display(400, 240, dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_30, pimoroni_demo_hdmi_cfg);

void setup() {
  Serial.begin(115200);
  //while(!Serial);
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;) digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }
}

void loop() {
}
