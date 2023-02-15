// Double-buffered 8-bit Adafruit_GFX-compatible framebuffer for PicoDVI.
// Allows animation without redraw flicker. Requires Adafruit_GFX >= 1.11.4

#include <PicoDVI.h>

// 8-bit currently supports 320x240 and 400x240 resolutions only.
DVIGFX8 display(DVI_RES_400x240p60, true, pimoroni_demo_hdmi_cfg);

#define N_BALLS 100 // 1-254 (not 255)
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

  // Randomize initial ball positions, velocities and colors
  for (int i=0; i<N_BALLS; i++) {
    display.setColor(i+1, 64 + random(192), 64 + random(192), 64 + random(192));
    ball[i].pos[0] = 10 + random(display.width() - 20);
    ball[i].pos[1] = 10 + random(display.height() - 20);
    do {
      ball[i].vel[0] = 2 - random(5);
      ball[i].vel[1] = 2 - random(5);
    } while ((ball[i].vel[0] == 0) && (ball[i].vel[1] == 0));
  }
  display.setColor(255, 0xFFFF); // Last palette entry = white
  display.swap(false, true); // Duplicate same palette into front & back buffers
}

void loop() {
  // Clear back framebuffer and draw balls (circles) there.
  display.fillScreen(0);
  for (int i=0; i<N_BALLS; i++) {
    display.fillCircle(ball[i].pos[0], ball[i].pos[1], 20, i + 1);
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
