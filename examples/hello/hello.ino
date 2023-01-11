#include <PicoDVI.h>

PicoDVI dvi(320, 240, VREG_VOLTAGE_1_20, dvi_timing_640x480p_60hz, pimoroni_demo_hdmi_cfg);

void setup() {
  dvi.begin();
}

void loop() {
}
