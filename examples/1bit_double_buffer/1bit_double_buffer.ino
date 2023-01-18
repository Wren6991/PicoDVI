// 1-bit Adafruit_GFX-compatible framebuffer for PicoDVI.

#include <PicoDVI.h>

//DVIGFX1 display(640, 480, true, dvi_timing_640x480p_60hz, VREG_VOLTAGE_1_30, pimoroni_demo_hdmi_cfg);
DVIGFX1 display(800, 480, true, dvi_timing_800x480p_60hz, VREG_VOLTAGE_1_30, pimoroni_demo_hdmi_cfg);

#define N_BALLS 100
struct {
  int16_t pos[2];
  int8_t  vel[2];
} ball[N_BALLS];

void setup() {
  Serial.begin(115200);
  //while(!Serial);
  if (!display.begin()) { // Blink LED if insufficient RAM
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;) digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }

  // Randomize initial ball positions and velocities
  for (int i=0; i<N_BALLS; i++) {
    ball[i].pos[0] = 10 + random(display.width() - 20);
    ball[i].pos[1] = 10 + random(display.height() - 20);
    do {
      ball[i].vel[0] = 4 - random(9);
      ball[i].vel[1] = 4 - random(9);
    } while ((ball[i].vel[0] == 0) && (ball[i].vel[1] == 0));
  }
}

void loop() {
  // Clear back framebuffer and draw balls (circles) there.
  display.fillScreen(0);

  for (int i=0; i<N_BALLS; i++) {
    display.drawCircle(ball[i].pos[0], ball[i].pos[1], 40, 1);
    // After drawing each one, update positions, bounce off edges.
    ball[i].pos[0] += ball[i].vel[0];
    if ((ball[i].pos[0] <= 0) || (ball[i].pos[0] >= display.width())) ball[i].vel[0] *= -1;
    ball[i].pos[1] += ball[i].vel[1];
    if ((ball[i].pos[1] <= 0) || (ball[i].pos[1] >= display.height())) ball[i].vel[1] *= -1;
  }
  // Swap front/back buffers, do not duplicate current screen state to next frame,
  // we'll draw it new from scratch each time.
  display.swap();
}
