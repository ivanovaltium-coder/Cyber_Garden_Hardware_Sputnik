// HEALTHMONITOR — Arduino UNO + ILI9341 (MCUFRIEND_kbv) + TouchScreen
// Приём данных с ESP32 по SoftwareSerial (RX = D10)
// Формат от ESP32: STEPS,TEMP,SYS,DIA,PULSE,STRESS,ACTIVITY,ECG

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <TouchScreen.h>
#include <SoftwareSerial.h>
#include <math.h>

MCUFRIEND_kbv tft;

// SoftwareSerial: RX = 10 (приём от ESP), TX = 11 (не обязателен)
SoftwareSerial espSerial(10, 11);

// ---------------- COLORS (RGB565) ----------------
#define TFT_BLACK   0x0000
#define TFT_WHITE   0xFFFF
#define TFT_YELLOW  0xFFE0
#define TFT_RED     0xF800
#define TFT_GREEN   0x07E0
#define TFT_CYAN    0x07FF
#define TFT_GREY    0x8410
#define TFT_DARKGREY 0x39E7
#define TFT_GRIDRED 0xF810

// ---------------- TOUCH (shield style) ----------------
#define YP A3
#define XM A2
#define YM 9
#define XP 8

// calibration (user values)
#define TS_MINX 338
#define TS_MAXX 816
#define TS_MINY 253
#define TS_MAXY 895

TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
#define MINPRESSURE 150
#define MAXPRESSURE 1000

// ---------------- UI / ECG state ----------------
int page = 1;
const int PAGES = 3;

const int ECG_LEFT = 20;
const int ECG_RIGHT = 300;
const int ECG_TOP = 50;
const int ECG_BOTTOM = 200;
int ecgX = ECG_LEFT;
int ecgPrevY = (ECG_TOP + ECG_BOTTOM) / 2;
unsigned long lastECGmillis = 0;
const int ECG_STEP_MS = 24;

// ECG template
const int TEMPLATE_LEN = 120;
int ecgTemplate[TEMPLATE_LEN];
int tplIndex = 0;

// ---------------- Data from ESP ----------------
int g_steps = 0;
float g_temp = 36.6;
int g_sys = 120, g_dia = 80;
int g_pulse = 72;
int g_stress = 40, g_activity = 30;
int g_hr = 72;
int g_ecgVal = 0; // small dynamic offset from ESP

// ---------------- Prototypes ----------------
void prepareECGTemplate();
void drawHeader(const char* title);
void drawFooter();
void drawPageIndicators();
void drawPage1();
void drawPage2();
void drawPage3();
void drawGrid();            // professional ECG grid
void updateECG();
bool readCalibratedTouch(int &sx, int &sy);
void readDataFromESP32();
void safePrintHR();        // helper to draw HR top-right (clears area)

// ---------------- Implementation ----------------

void prepareECGTemplate() {
  for (int i = 0; i < TEMPLATE_LEN; ++i) ecgTemplate[i] = 0;
  int pPos = 12, qPos = 40, rPos = 44, sPos = 48, tPos = 72;
  for (int i = -4; i <= 4; ++i) {
    int idx = pPos + i;
    if (idx >= 0 && idx < TEMPLATE_LEN) ecgTemplate[idx] += (int)(6 * exp(-0.5f * (i*i) / 8.0f));
  }
  ecgTemplate[qPos] -= 12;
  ecgTemplate[rPos] += 60; ecgTemplate[rPos-1] += 20; ecgTemplate[rPos+1] += 20;
  ecgTemplate[sPos] -= 16;
  for (int i = -8; i <= 8; ++i) {
    int idx = tPos + i;
    if (idx >= 0 && idx < TEMPLATE_LEN) ecgTemplate[idx] += (int)(18 * exp(-0.5f * (i*i) / 30.0f));
  }
}

// Header (no page text on right)
void drawHeader(const char* title) {
  tft.fillRect(0, 0, tft.width(), 28, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(8, 6);
  tft.print(title);
}

// Footer draws page indicator dots
void drawFooter() {
  int y = tft.height() - 18;
  tft.fillRect(0, y, tft.width(), 18, TFT_BLACK);
  drawPageIndicators();
}

void drawPageIndicators() {
  int baseX = tft.width()/2 - 28;
  int y = tft.height() - 9;
  for (int i = 1; i <= PAGES; ++i) {
    int x = baseX + (i-1)*20;
    if (i == page) tft.fillCircle(x, y, 5, TFT_GREEN);
    else tft.drawCircle(x, y, 5, TFT_GREY);
  }
}

// Page 1 — Vitals
void drawPage1() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("HEALTHMONITOR V.7.93 by Sputnik");
  tft.drawFastHLine(6, 28, tft.width()-12, TFT_DARKGREY);

  int x = 14, y = 44;
  tft.setTextSize(2);

  tft.setTextColor(TFT_GREY);
  tft.setCursor(x, y); tft.print("STEPS:");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(x + 110, y); tft.print(g_steps);

  y += 32;
  tft.setTextColor(TFT_GREY);
  tft.setCursor(x, y); tft.print("TEMP:");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(x + 110, y); tft.print(g_temp, 1); tft.print(" C");

y += 32;
  tft.setTextColor(TFT_GREY);
  tft.setCursor(x, y); tft.print("BP:");
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(x + 110, y);
  tft.print(g_sys); tft.print("/"); tft.print(g_dia); tft.print(" mmHg");

  y += 32;
  tft.setTextColor(TFT_GREY);
  tft.setCursor(x, y); tft.print("PULSE:");
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(x + 110, y); tft.print(g_pulse); tft.print(" bpm");

  drawFooter();
}

// draw single vertical bar and percent above
void drawBar(int left, int top, int width, int height, int value, uint16_t color) {
  tft.drawRect(left, top, width, height, TFT_DARKGREY);
  int step = height / 10;
  for (int i = 1; i < 10; ++i) tft.drawFastHLine(left+1, top + i*step, width-2, TFT_DARKGREY);
  int fillH = (value * height) / 100;
  if (fillH > 0) tft.fillRect(left+1, top+height - fillH +1, width-2, fillH-1, color);

  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  char buf[8]; sprintf(buf, "%d%%", value);
  tft.setCursor(left + (width/2)-12, top - 18);
  tft.print(buf);
}

void drawPage2() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("TRENDS: STRESS & ACTIVITY");

  int barW = 60, barH = 120, gap = 40;
  int left1 = 60;
  int left2 = left1 + barW + gap;
  int top = 60;

  drawBar(left1, top, barW, barH, g_stress, TFT_RED);
  drawBar(left2, top, barW, barH, g_activity, TFT_CYAN);

  // подписи ровно под столбиками (подогнаны X)
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(left1 + 8, top + barH + 8); tft.print("STRESS");
  tft.setCursor(left2 + 6, top + barH + 8); tft.print("ACTIVITY");

  drawFooter();
}

// Professional ECG grid: fine 5px grey + thicker red every 25px
void drawGrid() {
  // fill ECG area (black)
  tft.fillRect(ECG_LEFT-1, ECG_TOP-1, ECG_RIGHT-ECG_LEFT+3, ECG_BOTTOM-ECG_TOP+3, TFT_BLACK);

  // fine grid every 5 px (light)
  for (int x = ECG_LEFT; x <= ECG_RIGHT; x += 5) {
    tft.drawFastVLine(x, ECG_TOP, ECG_BOTTOM - ECG_TOP + 1, TFT_DARKGREY);
  }
  for (int y = ECG_TOP; y <= ECG_BOTTOM; y += 5) {
    tft.drawFastHLine(ECG_LEFT, y, ECG_RIGHT - ECG_LEFT + 1, TFT_DARKGREY);
  }

  // thick red grid every 25 px
  for (int x = ECG_LEFT; x <= ECG_RIGHT; x += 25) {
    tft.drawFastVLine(x, ECG_TOP, ECG_BOTTOM - ECG_TOP + 1, TFT_GRIDRED);
  }
  for (int y = ECG_TOP; y <= ECG_BOTTOM; y += 25) {
    tft.drawFastHLine(ECG_LEFT, y, ECG_RIGHT - ECG_LEFT + 1, TFT_GRIDRED);
  }

  // frame
  tft.drawRect(ECG_LEFT, ECG_TOP, ECG_RIGHT - ECG_LEFT, ECG_BOTTOM - ECG_TOP, TFT_WHITE);
}

// small HR print in top-right, erasing background first
void safePrintHR() {
  // clear small area at top-right
  int w = 60, h = 14;
  tft.fillRect(tft.width() - w - 4, 4, w, h, TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(tft.width() - 56, 6);
  tft.print("HR:");
  tft.print(g_hr);
}

void drawPage3() {
  tft.fillScreen(TFT_BLACK);
  drawHeader("ECG MONITOR");
  drawGrid();

  // small HR top-right
  safePrintHR();

  // init ECG drawing
  ecgX = ECG_LEFT;
  ecgPrevY = (ECG_TOP + ECG_BOTTOM) / 2;
  tplIndex = 0;

  drawFooter();
}

// update moving ECG line using template + external offset
void updateECG() {
  unsigned long now = millis();
  if (now - lastECGmillis < ECG_STEP_MS) return;
  lastECGmillis = now;

  // compute sample from template and external offset
  int tmpl = ecgTemplate[tplIndex];
  int mid = (ECG_TOP + ECG_BOTTOM) / 2;
  // combine template and incoming g_ecgVal (small offset)
  int newY = mid - (tmpl + g_ecgVal);

  // draw vertical column background (repaint grid for that column)
  int x = ecgX;
  for (int yy = ECG_TOP; yy <= ECG_BOTTOM; ++yy) {
    uint16_t col;
    if ((x - ECG_LEFT) % 25 == 0) col = TFT_GRIDRED;
    else if ((yy - ECG_TOP) % 25 == 0) col = TFT_GRIDRED;
    else if ((x - ECG_LEFT) % 5 == 0) col = TFT_DARKGREY;
    else if ((yy - ECG_TOP) % 5 == 0) col = TFT_DARKGREY;
    else col = TFT_BLACK;
    tft.drawPixel(x, yy, col);
  }

// draw ECG line segment
  if (ecgX > ECG_LEFT) {
    tft.drawLine(ecgX - 1, ecgPrevY, ecgX, newY, TFT_GREEN);
  } else {
    // first pixel
    tft.drawPixel(ecgX, newY, TFT_GREEN);
  }

  ecgPrevY = newY;
  ecgX++;
  tplIndex = (tplIndex + 1) % TEMPLATE_LEN;

  // reached right edge — clear ECG area and redraw grid & HR
  if (ecgX > ECG_RIGHT) {
    tft.fillRect(ECG_LEFT, ECG_TOP, ECG_RIGHT - ECG_LEFT + 1, ECG_BOTTOM - ECG_TOP + 1, TFT_BLACK);
    drawGrid();
    safePrintHR();
    ecgX = ECG_LEFT;
    ecgPrevY = (ECG_TOP + ECG_BOTTOM) / 2;
  }
}

// ---------------- Read data from ESP (SoftwareSerial on D10) ----------------
void readDataFromESP32() {
  if (espSerial.available()) {
    String line = espSerial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    // expected: STEPS,TEMP,SYS,DIA,PULSE,STRESS,ACTIVITY,ECG
    // parse by strtok (convert to char[])
    char buf[120];
    line.toCharArray(buf, sizeof(buf));
    char *tok = strtok(buf, ",");
    int idx = 0;
    // temporary holders
    int steps=0, sys=0, dia=0, pulse=0, stress=0, activity=0, ecg=0;
    float temp=0.0;
    while (tok != NULL && idx < 8) {
      switch (idx) {
        case 0: steps = atoi(tok); break;
        case 1: temp = atof(tok); break;
        case 2: sys = atoi(tok); break;
        case 3: dia = atoi(tok); break;
        case 4: pulse = atoi(tok); break;
        case 5: stress = atoi(tok); break;
        case 6: activity = atoi(tok); break;
        case 7: ecg = atoi(tok); break;
      }
      idx++;
      tok = strtok(NULL, ",");
    }
    if (idx >= 8) {
      // update globals
      g_steps = steps;
      g_temp = temp;
      g_sys = sys;
      g_dia = dia;
      g_pulse = pulse;
      g_stress = constrain(stress, 0, 100);
      g_activity = constrain(activity, 0, 100);
      g_hr = pulse;
      g_ecgVal = ecg;

      // refresh pages sensibly:
      if (page == 1) drawPage1(); // redraw vitals
      else if (page == 2) drawPage2(); // redraw bars
      else {
        // if on page3 just update HR display (avoid full redraw)
        safePrintHR();
      }
    }
  }
}

// touch reading with rotation(1) mapping (as discussed)
bool readCalibratedTouch(int &sx, int &sy) {
  long sumX=0, sumY=0; int n=0;
  for (int i=0;i<3;i++){
    TSPoint p = ts.getPoint();
    pinMode(XM, OUTPUT);
    pinMode(YP, OUTPUT);
    if (p.z > MINPRESSURE && p.z < MAXPRESSURE) {
      sumX += p.x; sumY += p.y; n++;
    }
    delay(6);
  }
  if (n==0) return false;
  int avgX = sumX / n;
  int avgY = sumY / n;
  int rawX = map(avgX, TS_MINX, TS_MAXX, 0, tft.width());
  int rawY = map(avgY, TS_MINY, TS_MAXY, 0, tft.height());
  // rotation(1) mapping used earlier
  sx = rawY;
  sy = tft.height() - rawX;
  if (sx < 0) sx = 0; if (sx > tft.width()) sx = tft.width();
  if (sy < 0) sy = 0; if (sy > tft.height()) sy = tft.height();
  return true;
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  espSerial.begin(9600);

  prepareECGTemplate();

  uint16_t ID = tft.readID();
  if (ID == 0xD3D3 || ID == 0xFFFF || ID == 0x0000) ID = 0x9341;
  tft.begin(ID);
  tft.setRotation(1);

  page = 1;
  drawPage1();
}

void loop() {
  // read incoming data (non-blocking)
  readDataFromESP32();

  // touch
  int tx, ty;
  if (readCalibratedTouch(tx, ty)) {
    // left half -> prev page, right half -> next page
    if (tx < tft.width() / 2) {
      page--;
      if (page < 1) page = PAGES;
    } else {
      page++;
      if (page > PAGES) page = 1;
    }
    if (page == 1) drawPage1();
    else if (page == 2) drawPage2();
    else if (page == 3) drawPage3();
    delay(300); // debounce
  }

  // ECG drawing only on page 3
  if (page == 3) updateECG();
}