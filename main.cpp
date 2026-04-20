// DojoClockv1_31

// NOTES: MUST "UPLOAD" css file (found in data folder) to firmware via littlefile system.
// NOTES: morenvsflash.csv with "board_build.partitions = morenvsflash.csv" in platformio.ini allocates upper non-standard portions of NVS survives reboots but not flash!!
// So... After Flash, preferences and programs will most likely need to be reentered.
//================================
// PROBLEM: (FIXED!) Fight bells single, wait, double is confusing.
// PROBLEM: (FIXED!): Use Light sensor results in black display
// PROBLEM: (FIXED!): Dark mode can go as low as 3 (of 255) and appears off, when it's not.
// PROBLEM: Add a cool random LED "wake up Effect" in the slowTimer myCycle routine??? WHY! No one's going to be there at sunrise! Out Darkness? "Lights on" maybe?
//================================

// DECLARATIONS
#include <Arduino.h>
//#include "esp_heap_caps.h" //Memory measurements

// ========== No web interface > Values Stored in Preferences ================
// Changed in the LCD setup and saved on change.
bool worldTime = 0;
bool useDST = 1;
char timeZone = 'C'; // E- Eastern, C- Central, M- Mountain, W-Western
uint8_t DSTStartSunday = 2;  // Second Sunday in March 2am Begin
uint8_t DSTStartMonth = 3;  // Second Sunday in March 2am Begin
uint8_t DSTEndSunday = 1; // First Sunday in November 2am End
uint8_t DSTEndMonth = 11; // First Sunday in November 2am End

#include <Preferences.h>
Preferences eeprom; // Global Preferences object for NVS access
// Structure to hold 'prefs' data
struct PrefsData {
    String ssid;
    String password;
    uint8_t colorClock; // 13
    uint8_t colorSeconds; // 4
    uint8_t colorMonth; // 8
    uint8_t colorDate; // 3
    uint8_t taskHour; // 3 , 24 hour time HOUR to sync clock
    uint8_t taskMinute; // 0 , 24 hour time MINUTE to sync clock
    uint8_t luxDeltaCovered; // 50 ,  % decline to indicate a sensor cover.
    int8_t  brightnessLateral; // 0 , + larger shifts line Left, - larger shifts line right. Y intercept of the slope! 
    uint8_t brightnessMax; // 255
    uint8_t brightnessMin; // 3 , 1 isn't really visible
    uint8_t brightnessStatic; // 50 , fastLED master: 1-255 8 bit, initial value used with NO light sensor
    uint8_t brightnessMinDark; // 15, In dark mode this is the minimum value brightness.
    uint8_t blendSpeed; // 40 , LED blend value 0-255 40 = ~2 seconds.
    uint8_t bellStrikeTime; // 18 , ms of relay engaging, 14ms is the quietest
    uint8_t neglectTime; // 120 , Minutes to return to clock mode
    uint8_t lcdBacklightTime; // 120 , Seconds to turn off LCD Backlight.
    uint8_t luxDarkMode; // 10, Sensor Lux value to go to dark mode.
    uint8_t nightDisplayOffTime; // 22, 10PM 
    bool    useBeeper;
    bool    useBell;
    bool    useLightSensor; // 1, If sensor fails, can be used to go forward.
    bool    nightDisplayOff; // 1, from 10pm to 6pm if dark, shut off LED display.
    float   brightnessSlope; // 0.25 , Larger numbers steepen the line, smaller decrease steepenes. 0.51 is rougly 45 deg angle intial value. 
    float   luxStabilityVariance; // 1.75 , % difference the samples need to be within to be "Stable."
} prefsData;

// Programs 0-9 each with 30 lines with CMD and DAT
// Define the number of character and byte elements per namespace
// With 60 elements total, it will be 30 pairs of char/byte
const int NUM_PAIR_ELEMENTS = 30;
const int TOTAL_NAMESPACE_ELEMENTS = NUM_PAIR_ELEMENTS * 2; // Should be 60

// Structure to hold data for each numbered namespace (programs 0-9)
struct NamespaceData {
    String title;
    char charElements[NUM_PAIR_ELEMENTS]; // Stores char values
    uint8_t byteElements[NUM_PAIR_ELEMENTS]; // Stores byte values
};

// Array to hold data for all 10 namespaces (programs 0-9) #10 is where "manual entry" program is stored.
NamespaceData namespaces[11]; // Should THIS be 10 ???!!!!?????!!!!???

#include <LittleFS.h> // File system

#include <WebServer.h>
WebServer server(80); // Create a web server on port 80
const char* APssid = "DojoClock"; // Replace with your desired AP name
const char* APpassword = "journey135"; // Replace with your desired password
bool wifiServerStatus = 0;

#include <htmlStyle.h> // HTML fragments for HTML construction.

// Bell and Beeper
#define BeeperPin 2
#define BellPin 13
uint16_t bellDelay = 250; //Minimum ms between rings.

uint8_t deviceMode = 99; //0-Boot Terminal, 1-Clock, 2-CntUp, 3-ManEntry, 4-Prog, 5-ProgSel, 6-SetupSel, 11-Date Settings, 12-Clock Settings, 13-DST Dates Settings, 14-Server On, 15-Sensor Data> Change to Terminal Device Mode 0 and set sensorDataOutput = 1, 16-Reboot, 30-ManEntry(Fight)?, 31-ManEntry(Bonus), 32-ManEntry(Rest), 33-ManEntry(Rounds), 111-Setup(Month), 112-Setup(Day), 113-Setup(Year), 121-Clock Time Entry, 131-DST Start Sunday, 132-DST Start Month, 133-DST End Month, 134-DST End Month, 255-Dark Mode

// Command Interp Variables
bool cmdStarted = 0;
bool cmdCompleted = 0;
uint8_t progCmd = 0;
uint8_t progNum = 0;
bool loopSet = 0;
uint8_t loopCount = 0;
bool readyForAction = 0;

#include <TinyTimer.h>
TinyTimer bootDelayWifiTimer; // After power loss the wifi might not have been raised before clock tried to connect.
TinyTimer cycleFastTimer; // Run every 100 mS
TinyTimer cycleMediumTimer; // Run every 250 mS
TinyTimer cycleSlowTimer; // Run every 1000 mS
TinyTimer luxSampleTimer; // Run every 2000 mS to get 3 samples over 6 seconds.
TinyTimer neglected;
TinyTimer myBacklightTimer;
TinyTimer napTimer;
TinyTimer sensorPressTimer;
TinyTimer specialEffectsTimer;

#include <CountUpDownTimer.h>
CountUpDownTimer timerD1(DOWN); //This is the only one that registers "End"
CountUpDownTimer timerU2(UP); // This is to show up times
CountUpDownTimer timerD3(DOWN); // Need this for blind down counts. This shows while timerD1 is offical expiring count.
bool useFight = 0;
uint8_t timeRegister[] = {0, 0, 0, 0}; // Order 01:23
uint8_t fightRegister[] = {0, 0, 0, 0}; // Order 01:23
uint8_t bonusRegister[] = {0, 0}; // order 0: tens, 1: ones
bool useBonus = 0;
bool blindBonus = 0;
bool blindStep = 0; // Another bonus step to do? 1 = Yes.
int blindBonusTime = 0; // Seconds
uint8_t restRegister[] = {0, 0, 0, 0}; // Order 01:23
bool useRest = 0;
uint8_t roundsRegister[] = {0, 1}; // order 0: tens, 1: ones
bool useRounds = 0;
bool roundsInc = 1; // 0 = decrement.
uint8_t roundsInterval = 1;
bool rounds2Loops = 0;
bool programPaused = 0;
//unsigned long intervalTime = 0;
//unsigned long intervalTarget = 0;

#include <WiFi.h>
uint8_t wifiTimeout = 10;
uint8_t wifiTimeoutDefault = 10;
bool wifiSuccess = 0;
bool wifiClientStatus = 0;
bool wifiAPStatus = 0;
IPAddress IP; // address of AP

#include <ESP32MatrixKeypad.h>
const char keymap[16] = {
  '1','2','3','A',
  '4','5','6','B',
  '7','8','9','C',
  'G','0','S','D'
};
uint8_t rowPins[4] = {12, 11, 10, 9};
uint8_t colPins[4] = {8, 7, 6, 5};
ESP32MatrixKeypad keypad(keymap, rowPins, colPins, 4, 4);
//ESP32MatrixKeypad keypad(keymap, rowPins, colPins, 4, 4, 25, 8);

//  #include <Keypad.h>
//  //define the symbols on the buttons of the keypads
//  char hexaKeys[4][4] = { //Rows, Columns
//    {'1','2','3','A'},
//    {'4','5','6','B'},
//    {'7','8','9','C'},
//    {'G','0','S','D'}
//  };
//  uint8_t rowPins[4] = {12, 11, 10, 9}; //connect to the row GPIO pins of the keypad {9, 8, 7, 6}
//  uint8_t colPins[4] = {8, 7, 6, 5}; //connect to the column GPIO pins of the keypad {5, 4, 3, 2}
//  Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, 4, 4); // initialize an instance of class Keypad

#include "time.h"
const char* ntpServer = "pool.ntp.org"; // This is Auto, closest option is "north-america.pool.ntp.org" with still others..
struct tm timeinfo; // Time struct to store RTC values for program use.
struct tm backupTimeinfo; // Time struct to store the backup time used in unsuccesful NTP Syncs
time_t backupTimeEpoch = 0;
//
bool NTPSuccess = 0;
//bool DST= 0;
//const int daylightOffset_sec = 3600; //Set it to 3600 seconds to observes Daylight saving time; otherwise, set it to 0.
uint8_t lastTaskRunDay = 40; // 40 isn't a valid option so it will run on first boot day!
uint8_t lastMidnightRunDay = 40;
uint8_t lastMiddayRunDay = 40; 

#include <Wire.h> //I2C protocol pin settings
#define SDApin 3 // GPIO_21
#define SCLpin 4 // GPIO_22

#include <BH1750.h> //I2C Light Sensor
BH1750 lightMeter;
uint8_t luxDelta = 1; // 0-100 % change from lastLux
float   lux = 1;
float   luxSamples[] = {0, 0, 0}; // Ambient light levels, use any one when roomLuxValid = 1!
uint8_t luxSampleNum = 0;
bool    luxRoomValid = 0;
float   brightnessTarget = 25; // BRIGHTNESS SHOULD BE HERE... Default Brightness is 25
bool    ledDisplayState = 1; // 0: Off (sleep LED off after 2am)

#include <LCDTerminal.h>
LCDTerminal terminal(0x27); // Creates the LiquidCrystal Instance called "lcd"
auto& lcd = terminal.getLCD(); // give direct access to LCDTerminal's internal "lcd" (liquidCrystal library) instance.
uint8_t lcdMode = 99;

// Custom character array.... https://maxpromer.github.io/LCD-Character-Creator/
uint8_t wifiSymbol[] = {
  0b01110, // Define a custom character using binary format
  0b10001,
  0b01110,
  0b10001,
  0b00100,
  0b01010,
  0b00000,
  0b00100
};
uint8_t clockSymbol[] = {
  0b00000,
  0b01110,
  0b10101,
  0b10101,
  0b10111,
  0b10001,
  0b01110,
  0b00000
};
uint8_t heartSymbol[] = {
  0b00000,
  0b00000,
  0b01010,
  0b11111,
  0b11111,
  0b01110,
  0b00100,
  0b00000
};
uint8_t playSymbol[] = {
  0b00000,
  0b01000,
  0b01100,
  0b01110,
  0b01111,
  0b01110,
  0b01100,
  0b01000
};
uint8_t pauseSymbol[] = {
  0b00000,
  0b01010,
  0b01010,
  0b01010,
  0b01010,
  0b01010,
  0b01010,
  0b01010
};
uint8_t blindSymbol[] = {
  0b10001,
  0b10001,
  0b10001,
  0b10001,
  0b11101,
  0b10101,
  0b10101,
  0b11101
};
uint8_t bellSymbol[] = {
  0b00000,
  0b00100,
  0b01110,
  0b01110,
  0b01110,
  0b11111,
  0b11111,
  0b00100
};
uint8_t sourceT = 0; //Program Running LCD data source, 0:Clock, 1:TimerD1, 2:TimerU2

#include <FastLED.h>
uint8_t sourceU = 1; // LED TOP data source, 0:Clock, 1:TimerD1, 2:TimerU2
uint8_t sourceV = 0; // LED BOTTOM RIGHT data source, 0:Clock, 1:TimerD1, 2:TimerU2
uint8_t sourceW = 0; // LED BOTTOM LEFT data source, 0:Clock Seconds, 1:TimerU2 Hours, 2:RoundsRegister
FASTLED_USING_NAMESPACE // this is to make coding/referencing shorter.

// FOR Onboard RGB LED ================
CRGB onboardled[2];// 0 = actual LED color, 1 = "Last state" for pauses

// FOR LED DISPLAY ====================
#define DATA_PIN    14
#define LED_TYPE    WS2812B // Use this for W2815 type also.
#define COLOR_ORDER GRB
#define NUM_LEDS    576
CRGB leds[NUM_LEDS];
#define FRAMES_PER_SECOND  120 //120 // I had 2000!
uint8_t BRIGHTNESS; // Constantly Changing while light sensor is used!

// For LED Positive space Digits =================
bool useLedDigitMask = 1; // Easily turn off digits for full display effects.
uint8_t fastEffectTime = 5;
bool upperColon = 1;
bool lowerColon = 1;

uint8_t color100 = 13;// digits 0,1
uint8_t colorLC = 13; // lower colon
uint8_t color102 = 13;// digits 2,3
bool  split100_102 = 1; // should all combined 103 be used to color LEDs?
uint8_t color103 = 13;// digits 0,1,2,3 & LC
uint8_t color104 = 13;// digits 4,5
uint8_t color106 = 13;// digits 6,7,8,9 & UC
bool    paintEntire = 0;
uint8_t color255 = 13;

uint8_t color100Last = 13;// digits 0,1
uint8_t color102Last = 13;// digits 2,3
uint8_t colorLCLast  = 13;// lower colon
uint8_t color104Last = 13;// digits 4,5
uint8_t color106Last = 13;// digits 6,7,8,9 & UC

int startingDigitAddress[10] = 
  { //fixture address of first LED in display digit
  0, 41, 84, 125, 166, 204, 242, 325, 410, 493
  }; 
  // D0:0-40(41), D1:41-81(41), D2:84-124(41), D3:125-165(41), D4:166-203(38), D5:204-241(38), D6:242-324(83), D7:325-407(83), D8:410-492(83), D9:493-576(83)
  // U = 242-576(334), V = 0-165(166), W = 166-241(76)
int startingColonAddress[2] = 
  { //fixture address of first LED in colon seperator
  82, 408
  };
uint8_t ledDigit[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}; // This IS the VALUE stored at address 0 [ lower right] moving left to 5, 6 is upper Right, moving left to 9.
uint8_t ledMode = 99;

const uint8_t maxColorNum = 14; // used in HTML color entry checking and program CMD U, V, W random color selection and error checking.
// Color Pallete: colors are are vertical example: color 0 is 0,0,0. 
const uint8_t Red[14] = {
    0,    // 0: Black
    255,  // 1: Red
    240,  // 2: Orange
    255,  // 3: Yellow
    0,    // 4: Green
    0,    // 5: Dark Green
    0,    // 6: Teal
    0,    // 7: Cyan
    0,    // 8: Blue
    65,   // 9: Indigo
    110,  // 10: Purple
    240,  // 11: Magenta
    220,  // 12: Pink
    245   // 13: White
};

const uint8_t Green[14] = {
    0,   // 0: Black
    0,   // 1: Red
    110, // 2: Orange
    255, // 3: Yellow
    255, // 4: Green
    110, // 5: Dark Green
    110, // 6: Teal
    240, // 7: Cyan
    0,   // 8: Blue
    0,   // 9: Indigo
    0,   // 10: Purple
    0,   // 11: Magenta
    80,  // 12: Pink
    245  // 13: White
};

const uint8_t Blue[14] = {
    0,   // 0: Black
    0,   // 1: Red
    0,   // 2: Orange
    0,   // 3: Yellow
    0,   // 4: Green
    0,   // 5: Dark Green
    110, // 6: Teal
    240, // 7: Cyan
    255, // 8: Blue
    115, // 9: Indigo
    110, // 10: Purple
    240, // 11: Magenta
    100, // 12: Pink
    245  // 13: White
};

// For LED Effects??? ===============================
uint8_t gCurrentPatternNumber = 0; // Index number of which pattern is current
uint8_t gHue = 0; // rotating "base color" used by many of the patterns
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))
//=========================================================================
void solidColorInstant(int startIndex, int endIndex, uint8_t color){
  for (int i = startIndex; i <= endIndex; i++){ leds[i] = CRGB(Red[color],Green[color],Blue[color]); }
}
void solidColorRange(int startIndex, int endIndex, uint8_t color) {
  // static uint8_t blendAmount = 0;   // 0 → 255 fade‑in progress
  CRGB target = CRGB(Red[color],Green[color],Blue[color]);
  for (int i = startIndex; i <= endIndex; i++) { 
    // nblend: blend current LED toward target by fadeAmount (0–255) 
    nblend(leds[i], target, prefsData.blendSpeed); //10 and 20 is ~2 second blend! 40 = ?
  }
}
void rainbowRangeInstant(int startIndex, int endIndex, uint8_t spacing) {
  fill_rainbow(&leds[startIndex], (endIndex - startIndex + 1), gHue, spacing);
}
void rainbowRangeBlend(int startIndex, int endIndex, uint8_t spacing) {
// call with blendAmount 0..255 (e.g., 32 subtle, 128 medium, 255 instant)
  int len = endIndex - startIndex + 1;
  for (int i = 0; i < len; ++i) {
    // compute the rainbow hue for this pixel
    uint8_t hue = gHue + i * spacing;
    CRGB target = CHSV(hue, 255, 255);

    // blend target into the existing pixel color in place
    // nblend(dest, src, amount) where amount is 0..255
    nblend(leds[startIndex + i], target, prefsData.blendSpeed);
  }
  //  How to use
  //    Subtle blend: rainbowRangeBlend(0, NUM_LEDS-1, 4, 16);
  //    Medium blend: rainbowRangeBlend(0, NUM_LEDS-1, 4, 64);
  //    Instant (current behavior): rainbowRangeBlend(0, NUM_LEDS-1, 4, 255);
}
void autoColorBlend(int startIndex, int endIndex, uint8_t spacing) {
  // This blends whole section colors through the rainbows from previous effect.
  for (int i = startIndex; i <= endIndex; i++) {
    CRGB target = CHSV(gHue, 255, 255);
    nblend(leds[i], target, prefsData.blendSpeed);
    //gHue += spacing;
 }
}
unsigned long strobeEnd[NUM_LEDS] = {0};
void eiffelSparkle(uint8_t chanceOfSparkle, uint8_t color) {
  unsigned long now = millis();
  // Randomly trigger new sparkles
  for (int i = 0; i < NUM_LEDS; i++) {
    if (random8() < chanceOfSparkle) {

      // Random flash duration: 150–300 ms
      uint16_t flashDuration = random16(150, 300);

      strobeEnd[i] = now + flashDuration;
    }
  }
  // Render sparkles
  for (int i = 0; i < NUM_LEDS; i++) {
    if (now < strobeEnd[i]) {
      // Brightness override: full punchy white / color
      CRGB intenseWhite = CRGB(Red[color], Green[color], Blue[color]);
      //CRGB intenseWhite = CRGB(255, 255, 255);
      intenseWhite.nscale8_video(255);
      leds[i] = intenseWhite;
    } else {
      leds[i] = CRGB::Black;
    }
  }
}
void confettiPops() {
  fadeToBlackBy(leds, NUM_LEDS, 10);

  // number of confetti pops per frame
  const uint8_t pops = 8;  

  for (uint8_t i = 0; i < pops; i++) {
    int pos = random16(NUM_LEDS);
    leds[pos] += CHSV(gHue + random8(64), 200, 255);
  }
}
void confettiClusters() { // Bigger Sparkles
  fadeToBlackBy(leds, NUM_LEDS, 10);

  int pos = random16(NUM_LEDS);
  CHSV color = CHSV(gHue + random8(64), 200, 255);

  // center pixel
  leds[pos] += color;

  // neighbors for a "burst"
  if (pos > 0) leds[pos - 1] += color;
  if (pos < NUM_LEDS - 1) leds[pos + 1] += color;
}
void confettiDrift() {
  fadeToBlackBy(leds, NUM_LEDS, 3);  // fade even slower
  // inject multiple sparkles per frame
  const uint8_t pops = 4;
  for (uint8_t i = 0; i < pops; i++) {
    int pos = random16(NUM_LEDS);
    leds[pos] = CHSV(gHue + random8(64), 200, 255);
  }

  // drift motion
  for (int i = NUM_LEDS - 1; i > 0; i--) {
    leds[i] = leds[i - 1].fadeToBlackBy(20);  // less fade = more lit pixels
  }
}
void confettiParty() {
  fadeToBlackBy(leds, NUM_LEDS, 20);

  for (int i = 0; i < NUM_LEDS; i++) {
    if (random8() < 10) {  // 10/256 chance per LED
      leds[i] = CHSV(gHue + random8(64), 200, 255);
    }
  }
}
void sinelonWide() {
  fadeToBlackBy(leds, NUM_LEDS, 20);  // very soft fade

  int pos = beatsin16(13, 0, NUM_LEDS - 1);

  // wide sweep
  for (int i = -10; i <= 10; i++) {
    int idx = pos + i;
    if (idx < 0 || idx >= NUM_LEDS) continue;

    uint8_t brightness = 255 - (abs(i) * 12);
    leds[idx] = CHSV(gHue, 200, brightness);
  }
}
void bpm() { // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
  uint8_t BeatsPerMinute = 62;
  CRGBPalette16 palette = PartyColors_p;
  uint8_t beat = beatsin8( BeatsPerMinute, 64, 255);
  for( int i = 0; i < NUM_LEDS; i++) { //9948
    leds[i] = ColorFromPalette(palette, gHue+(i*2), beat-gHue+(i*10));
  }
}
void bpm_hsv(uint8_t color1_H, uint8_t color1_S, uint8_t color2_H, uint8_t color2_S) {

    uint8_t BPM = 140;

    // Beat value oscillates between 64–255
    uint8_t beat = beatsin8(BPM, 64, 255);

    for (int i = 0; i < NUM_LEDS; i++) {

        bool useFirst = (i % 2 == 0);

        if (useFirst) {
            leds[i] = CHSV(color1_H, color1_S, beat);
        } else {
            leds[i] = CHSV(color2_H, color2_S, beat);
        }
    }
}
void bpm_colors(uint8_t color1, uint8_t color2) {
    uint8_t BPM = 140;
    uint8_t beat = beatsin8(BPM, 64, 255);

    for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t c = (i % 2 == 0) ? color1 : color2;

        leds[i] = CRGB(
            (Red[c]   * beat) / 255,
            (Green[c] * beat) / 255,
            (Blue[c]  * beat) / 255
        );
    }
}
void juggle() {
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy( leds, NUM_LEDS, 20);
  uint8_t dothue = 0;
  for( int i = 0; i < 8; i++) {
    leds[beatsin16( i+7, 0, NUM_LEDS-1 )] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}
void sloshColors(uint8_t BPM, uint8_t color1, uint8_t color2) {
    static uint32_t lastUpdate = 0;
    static uint32_t phase = 0;   // 0–65535 full cycle

    uint32_t now = millis();
    uint32_t dt = now - lastUpdate;
    lastUpdate = now;

    // One full slosh cycle = 2 beats
    uint32_t beatInterval = 60000UL / BPM;
    uint32_t cycleMs = beatInterval * 2;

    // Advance phase with perfect wrap
    // phase increment = (dt / cycleMs) * 65536
    uint32_t inc = (uint64_t)dt * 65536ULL / cycleMs;
    phase = (phase + inc) & 0xFFFF;

    // Convert phase → offset 0–10–0 triangle wave
    // phase 0–32767 = forward, 32768–65535 = backward
    uint16_t p = phase;

    int offset;
    if (p < 32768) {
        // rising 0 → 10
        offset = (p * 10L) / 32768;
    } else {
        // falling 10 → 0
        uint32_t t = p - 32768;
        offset = 10 - ((t * 10L) / 32768);
    }

    // Render repeating 10/10 hard‑edge pattern
    for (int i = 0; i < NUM_LEDS; i++) {
        int shifted = (i - offset) % 20;
        if (shifted < 0) shifted += 20;

        leds[i] = (shifted < 10)
            ? CRGB(Red[color1], Green[color1], Blue[color1])
            : CRGB(Red[color2], Green[color2], Blue[color2]);
    }
}
void maskDigit(uint8_t address, uint8_t digit){ // Display Digit Address, Digit to display.
  uint8_t a = 0;
  uint8_t b = 0;
  uint8_t c = 0;
  uint8_t d = 0;
  uint8_t e = 0;
  uint8_t f = 0;
  switch (digit) {
  case 0: //--------------------------------------------
    if (address >= 0 && address <= 3){
      a = 1;
    }
    if (address == 4 || address == 5) {
      a = 0;
    }
    if (address >= 6 && address < 10) {
      a = 7;
    } 
    for (int i = startingDigitAddress[address]; i < startingDigitAddress[address] + 4 + a; i++){
    leds[i] = CRGB::Black;
    }
    break;   
  case 1: //--------------------------------------------
    if (address >= 0 && address <= 3){
      a = 2;
      b = 2;
      c = 3;
    }
    if (address == 4 || address == 5) {
      a = 0;
      b = 0;
      c = 0;
    }
    if (address >= 6 && address < 10) {
      a = 26;
      b = 38;
      c = 45;
    }
    for (int i = startingDigitAddress[address]; i < startingDigitAddress[address] + 20 + a; i++){
    leds[i] = CRGB::Black;
    }
    for (int i = startingDigitAddress[address] + 34 + b ; i < startingDigitAddress[address] + 38 + c; i++){
    leds[i] = CRGB::Black;
    }
    break; 
  case 2: //-------------------------------------------- TRUE digit archive code
    if (address >= 0 && address <= 3){
      a = 1;
      b = 1;
      c = 2;
      d = 2;
    }
    if (address == 4 || address == 5) {
      a = 0;
      b = 0;
      c = 0;
      d = 0;
    }
    if (address >= 6 && address < 10) {
      a = 13;
      b = 19;
      c = 32;
      d = 38;
    } 
    for (int i = startingDigitAddress[address] + 11 + a ; i < startingDigitAddress[address] + 17 + b; i++){  //  for (int i = startingDigitAddress[address] + 11 + a ; i < startingDigitAddress[address] + 16 + b; i++){
    leds[i] = CRGB::Black;
    }
    for (int i = startingDigitAddress[address] + 28 + c ; i < startingDigitAddress[address] + 34 + d; i++){   //  for (int i = startingDigitAddress[address] + 28 + c ; i < startingDigitAddress[address] + 33 + d; i++){
    leds[i] = CRGB::Black;
    }
    break;
  case 3:
      if (address >= 0 && address <= 3){
      a = 1;
      b = 1;
      //c = 1;
      //d = 1;
    }
    if (address == 4 || address == 5) {
      a = 0;
      b = 0;
      //c = 0;
      //d = 0;
    }
    if (address >= 6 && address < 10) {
      a = 7;
      b = 19; //b = 13;
      //c = 13;
      //d = 19;
    } 
    for (int i = startingDigitAddress[address] + 4 + a ; i < startingDigitAddress[address] + 17 + b; i++){ // for (int i = startingDigitAddress[address] + 5 + a ; i < startingDigitAddress[address] + 10 + b; i++){
    leds[i] = CRGB::Black;
    }
    //for (int i = startingDigitAddress[address] + 11 + c ; i < startingDigitAddress[address] + 16 + d; i++){
    //leds[i] = CRGB::Black;
    //}
    break;  
  case 4:
    if (address >= 0 && address <= 3){
      a = 1;
      b = 1;
      c = 1;
      d = 2;
      e = 2;
      f = 3;
    }
    if (address == 4 || address == 5) {
      a = 0;
      b = 0;
      c = 0;
      d = 0;
      e = 0;
      f = 0;
    }
    if (address >= 6 && address < 10) {
      a = 7;
      b = 13;
      c = 19;
      d = 26;
      e = 38;
      f = 45;
    } 
    for (int i = startingDigitAddress[address] + 4 + a; i < startingDigitAddress[address] + 10 + b; i++){
    leds[i] = CRGB::Black;
    }
    for (int i = startingDigitAddress[address] + 17 + c ; i < startingDigitAddress[address] + 21 + d; i++){
    leds[i] = CRGB::Black;
    }
    for (int i = startingDigitAddress[address] + 34 + e ; i < startingDigitAddress[address] + 38 + f; i++){
    leds[i] = CRGB::Black;
    }
    break;
  case 5:
    if (address >= 0 && address <= 3){
      a = 1;
      b = 1;
      c = 2;
      d = 2;
    }
    if (address == 4 || address == 5) {
      a = 0;
      b = 0;
      c = 0;
      d = 0;
    }
    if (address >= 6 && address < 10) {
      a = 7;
      b = 13;
      c = 26;
      d = 32;
    } 
    for (int i = startingDigitAddress[address] + 4 + a; i < startingDigitAddress[address] + 10 + b; i++){ // for (int i = startingDigitAddress[address] + 5 + a; i < startingDigitAddress[address] + 10 + b; i++){
    leds[i] = CRGB::Black;
    }
    for (int i = startingDigitAddress[address] + 21 + c ; i < startingDigitAddress[address] + 27 + d; i++){ // for (int i = startingDigitAddress[address] + 22 + c ; i < startingDigitAddress[address] + 27 + d; i++){
    leds[i] = CRGB::Black;
    }
    break;
  case 6:
    if (address >= 0 && address <= 3){
      a = 2;
      b = 2;
    }
    if (address == 4 || address == 5) {
      a = 0;
      b = 0;
    }
    if (address >= 6 && address < 10) {
      a = 26;
      b = 32;
    } 
    for (int i = startingDigitAddress[address] + 21 + a; i < startingDigitAddress[address] + 27 + b; i++){ // for (int i = startingDigitAddress[address] + 22 + a; i < startingDigitAddress[address] + 27 + b; i++){
    leds[i] = CRGB::Black;
    }
    break;
  case 7:
    if (address >= 0 && address <= 3){
      a = 1;
      b = 2;
      c = 3;
    }
    if (address == 4 || address == 5) {
      a = 0;
      b = 0;
      c = 0;
    }
    if (address >= 6 && address < 10) {
      a = 19;
      b = 38;
      c = 45;
    } 
    for (int i = startingDigitAddress[address]; i < startingDigitAddress[address] + 17 + a; i++){ // for (int i = startingDigitAddress[address]; i < startingDigitAddress[address] + 16 + a; i++){
    leds[i] = CRGB::Black;
    }
    for (int i = startingDigitAddress[address] + 34 + b ; i < startingDigitAddress[address] + 38 + c; i++){
    leds[i] = CRGB::Black;
    }
    break;
  case 8:
    // Apply no mask, ALL pixels lit!
    break;
  case 9:
    if (address >= 0 && address <= 3){
      a = 1;
      b = 1;
      c = 2;
      d = 3;
    }
    if (address == 4 || address == 5) {
      a = 0;
      b = 0;
      c = 0;
      d = 0;
    }
    if (address >= 6 && address < 10) {
      a = 7;
      b = 13;
      c = 38;
      d = 45;
    } 
    for (int i = startingDigitAddress[address] + 4 + a; i < startingDigitAddress[address] + 10 + b; i++){
    leds[i] = CRGB::Black;
    }
    for (int i = startingDigitAddress[address] + 34 + c ; i < startingDigitAddress[address] + 38 + d; i++){
    leds[i] = CRGB::Black;
    }
    break;
  case 10: // 10 is "mask everything", show nothing.
    if (address >= 0 && address <= 3) {
      a = 41;
    }
    if (address == 4 || address == 5) {
      a = 38;
    }
    if (address >= 6 && address < 10) {
      a = 83;
    } 
    for (int i = startingDigitAddress[address]; i < startingDigitAddress[address] + a; i++){
    leds[i] = CRGB::Black;
    }
    break;
  default: break;
  }
}
void initializePreferences() {
  // Load 'prefs'
  eeprom.begin("prefs", false); // 'false' for read-write mode
  prefsData.ssid = eeprom.getString("ssid", "MyWiFissid");
  prefsData.password = eeprom.getString("password", "MyPassword");
  prefsData.colorClock = eeprom.getUChar("colorClock", 13);
  prefsData.colorSeconds = eeprom.getUChar("colorSeconds", 4);
  prefsData.colorMonth = eeprom.getUChar("colorMonth", 8);
  prefsData.colorDate = eeprom.getUChar("colorDate", 3);
  prefsData.taskHour = eeprom.getUChar("taskHour", 2); // 2 am
  prefsData.taskMinute = eeprom.getUChar("taskMinute", 0);
  prefsData.luxDeltaCovered = eeprom.getUChar("luxDeltaCovered", 50);
  prefsData.brightnessLateral = eeprom.getUChar("brightnessLateral", 0);
  prefsData.brightnessMax = eeprom.getUChar("brightnessMax", 255);
  prefsData.brightnessMin = eeprom.getUChar("brightnessMin", 3);
  prefsData.brightnessStatic = eeprom.getUChar("brightnessStatic", 50);
  prefsData.brightnessMinDark = eeprom.getUChar("brightnessMinDark", 15);
  prefsData.blendSpeed = eeprom.getUChar("blendSpeed", 40);
  prefsData.bellStrikeTime = eeprom.getUChar("bellStrikeTime", 18);
  prefsData.neglectTime = eeprom.getUChar("neglectTime", 120);
  prefsData.lcdBacklightTime = eeprom.getUChar("lcdBacklightTime", 120); //120
  prefsData.luxDarkMode = eeprom.getUChar("luxDarkMode", 4); //4
  prefsData.nightDisplayOffTime = eeprom.getUChar("nightDisplayOffTime", 22); //22
  prefsData.useBeeper = eeprom.getBool("useBeeper", 0);
  prefsData.useBell = eeprom.getBool("useBell", 1);
  prefsData.nightDisplayOff = eeprom.getBool("nightDisplayOff", 1);
  prefsData.useLightSensor = eeprom.getBool("useLightSensor", 1);
  prefsData.brightnessSlope = eeprom.getFloat("brightnessSlope", 0.25);
  prefsData.luxStabilityVariance = eeprom.getFloat("luxStabilityVariance", 1.75);
  //------------------------------------------------
  timeZone = eeprom.getChar("timeZone", 'C');
  useDST = eeprom.getBool("useDST", true);
  worldTime = eeprom.getBool("worldTime", false);
  DSTStartSunday = eeprom.getChar("DSTStartSunday", 2);  // Second Sunday in March 2am Begin
  DSTStartMonth = eeprom.getInt("DSTStartMonth", 3);  // Second Sunday in March 2am Begin
  DSTEndSunday = eeprom.getChar("DSTEndSunday", 1); // First Sunday in November 2am End
  DSTEndMonth = eeprom.getInt("DSTEndMonth", 11); // First Sunday in November 2am End

  eeprom.end();
  
  // Default titles for the program 0-9 namespaces
  const char* defaultTitles[] = {
    "Prog 0", "Prog 1", "Prog 2", "Prog 3", "Prog 4",
    "Prog 5", "Prog 6", "Prog 7", "Prog 8", "Prog 9"
  };
  
  // Load data Q0 for each program namespace (0-9)
  for (int i = 0; i < 10; ++i) {
    String nsName = String(i); // Namespace name (e.g., "0", "1")
    eeprom.begin(nsName.c_str(), false);
    namespaces[i].title = eeprom.getString("title", defaultTitles[i]);
    for (int j = 0; j < NUM_PAIR_ELEMENTS; ++j) {
        String charKey = String(j * 2);     // Key for char element (0, 2, ..., 58)
        String byteKey = String(j * 2 + 1); // Key for byte element (1, 3, ..., 59)
        //  // Default values. 'j % 26' cycles A-Z for chars. 'j + 1' for bytes (1-30).
        //  char defaultChar = 'A' + (j % 26);
        //  uint8_t defaultByte = (uint8_t)(j + 1);
        namespaces[i].charElements[j] = eeprom.getChar(charKey.c_str(), 'Q');
        namespaces[i].byteElements[j] = eeprom.getUChar(byteKey.c_str(), 0);
    }
    eeprom.end();
  }
  //Manual Entry Program #10 is set up here:
  namespaces[10].title = "MANUAL";
  namespaces[10].charElements[0] = 'V'; // LED V source clock (0), color 14 (Rainbow)
  namespaces[10].byteElements[0] = 14;
  namespaces[10].charElements[1] = 'W'; // LED W source Rounds (2), color 8 (blue)
  namespaces[10].byteElements[1] = 208;
  namespaces[10].charElements[2] = 'L';
  namespaces[10].byteElements[2] = 1; // 1 time by default, unless Rounds are set.
  namespaces[10].charElements[3] = 'U'; // LED U source timerD1 (1), color 04 (green)
  namespaces[10].byteElements[3] = 104;
  namespaces[10].charElements[4] = 'A';
  namespaces[10].byteElements[4] = 0;
  namespaces[10].charElements[5] = 'N';
  namespaces[10].byteElements[5] = 2;
  namespaces[10].charElements[6] = 'H';
  namespaces[10].byteElements[6] = 1;
  namespaces[10].charElements[7] = 'a';
  namespaces[10].byteElements[7] = 0;
  namespaces[10].charElements[8] = 'H';
  namespaces[10].byteElements[8] = 2;
  namespaces[10].charElements[9] = 'N';
  namespaces[10].byteElements[9] = 2;
  namespaces[10].charElements[10] = 'U'; // LED U part source timerD1, color 01 (red)
  namespaces[10].byteElements[10] = 101;
  namespaces[10].charElements[11] = 'M';
  namespaces[10].byteElements[11] = 0;
  namespaces[10].charElements[12] = 'r';
  namespaces[10].byteElements[12] = 0;
  namespaces[10].charElements[13] = 'l';
  namespaces[10].byteElements[13] = 0;
  namespaces[10].charElements[14] = 'Q';
  namespaces[10].byteElements[14] = 0;
}
void handleRoot() {
    String html = HTML_PARTA;
    html += "DojoClock-Home";
    html += HTML_PARTB;
    html += "DojoClock</h1>";
    html += "<div class='nav-links'>";
    html += "<a href='/programs'>Programs</a><br>"; // Link to Programs page
    html += "<a href='/wifi'>WiFi Settings</a><br>"; // Link to Wifi page
    html += "<a href='/prefs'>Preferences</a><br>"; // Link to Preferences page
    html += "</div></div>";
    html += HTML_FOOTER;
    server.send(200, "text/html", html);
}
void handlePrograms() {
    String html = HTML_PARTA;
    html += "DojoClock-Programs";
    html += HTML_PARTB;
    html += "Programs</h1>";
    html += "<div class='nav-links'>";
    for (int i = 0; i < 10; ++i) {
        // Link to each program namespace page
        html += "<a href='/program" + String(i) + "'>Prog " + String(i) + ": " + namespaces[i].title + "</a><br>";
    }
    html += "</div>";
    html += "<h2 class='home-link'><a href='/'>Back to Home</a></h2>";
    html += "</div>";
    html += HTML_FOOTER;
    server.send(200, "text/html", html);
}
void handleWifi() {
    String html = HTML_PARTA;
    html += "DojoClock-Wifi";
    html += HTML_PARTB;
    html += "Wifi Settings</h1>";
    html += "<form action='/submit_prefs' method='POST'>";
    html += "<div class='field-row'>";
    html += "<div class='field-item'><label for='ssid'>SSID:</label><input type='text' id='ssid' name='ssid' value='" + prefsData.ssid + "'></div>";
    html += "<div class='field-item'><label for='password'>Password:</label><input type='text' id='password' name='password' value='" + prefsData.password + "'></div>";
    html += "</div>";
    html += "<input type='submit' value='Save Values'>";
    html += "</form>";
    html += "<h2 class='home-link'><a href='/'>Back to Home</a></h2>";
    html += "</div>";
    html += HTML_FOOTER;
    server.send(200, "text/html", html);
}
void handlePrefs() {
    String html = HTML_PARTA;
    html += "DojoClock-Preferences";
    html += HTML_PARTB;
    html += "Preferences</h1>";
    html += "<form action='/submit_prefs' method='POST'>";
    html += "<p><b>--- CLOCK COLORS ---</b></p>";
    html += "<div class='field-item'><label for='colorClock'>Time Color:</label><input type='number' id='colorClock' name='colorClock' value='" + String(prefsData.colorClock) + "' min='0' max='" + String(maxColorNum) + "'></div>";
    html += "<div class='field-item'><label for='colorSeconds'>Seconds Color:</label><input type='number' id='colorSeconds' name='colorSeconds' value='" + String(prefsData.colorSeconds) + "' min='0' max='" + String(maxColorNum) + "'></div>";
    html += "<div class='field-item'><label for='colorMonth'>Month Color:</label><input type='number' id='colorMonth' name='colorMonth' value='" + String(prefsData.colorMonth) + "' min='0' max='" + String(maxColorNum) + "'></div>";
    html += "<div class='field-item'><label for='colorDate'>Date Color:</label><input type='number' id='colorDate' name='colorDate' value='" + String(prefsData.colorDate) + "' min='0' max='" + String(maxColorNum) + "'></div>";
    html += "<p> 0: Off, 1: Red, 2: Orange, 3: Yellow, 4: Green, 5: Dark Green, 6: Teal, 7: Cyan, 8: Blue, 9: Indigo, 10: Purple, 11: Magenta, 12: Pink, 13: White, 14: Rainbow</p>";
    html += "<p><b>--- DISPLAY BEHAVIOR ---</b></p>";
    html += "<div class='field-item'><label for='brightnessStatic'>Master Brightness:</label><input type='number' id='brightnessStatic' name='brightnessStatic' value='" + String(prefsData.brightnessStatic) + "' min='0' max='255'></div>";
    html += "<p>Constant value, no sensor</p>";
    html += "<div class='field-item'><label for='blendSpeed'>Blend Speed:</label><input type='number' id='blendSpeed' name='blendSpeed' value='" + String(prefsData.blendSpeed) + "' min='0' max='255'></div>";
    html += "<p><b>40</b>, = 2 seconds for effects</p>";
    
    html += "<p>--- OTHER ---</p>";
    html += "<div class='field-item'><label for='useBeeper'>Use Key Beeps:</label><input type='checkbox' id='useBeeper' name='useBeeper' " + String(prefsData.useBeeper ? "checked" : "") + "></div>";
    html += "<div class='field-item'><label for='useBell'>Use Bell:</label><input type='checkbox' id='useBell' name='useBell' " + String(prefsData.useBell ? "checked" : "") + "></div>";
    html += "<div class='field-item'><label for='bellStrikeTime'>Bell Strike Time:</label><input type='number' id='bellStrikeTime' name='bellStrikeTime' value='" + String(prefsData.bellStrikeTime) + "' min='0' max='255'></div>";
    html += "<p><b>18</b>, ms relay powered</p>";
    html += "<div class='field-item'><label for='lcdBacklightTime'>LCD Backlight Time:</label><input type='number' id='lcdBacklightTime' name='lcdBacklightTime' value='" + String(prefsData.lcdBacklightTime) + "' min='0' max='255'></div>";
    html += "<p><b>120</b>, sec to off</p>";
    html += "<div class='field-item'><label for='neglectTime'>Neglect Timer:</label><input type='number' id='neglectTime' name='neglectTime' value='" + String(prefsData.neglectTime) + "' min='0' max='255'></div>";
    html += "<p><b>120</b>, min back to clock</p>";
    html += "<p>--- WEBSYNC TIME (2am) ---</p>";
    html += "<div class='field-item'><label for='taskHour'>Update Hour:</label><input type='number' id='taskHour' name='taskHour' value='" + String(prefsData.taskHour) + "' min='0' max='24'></div>";
    html += "<div class='field-item'><label for='taskMinute'>Update Minute:</label><input type='number' id='taskMinute' name='taskMinute' value='" + String(prefsData.taskMinute) + "' min='0' max='59'></div>";
    
    html += "<p>--- LIGHT SENSOR USAGE ---</p>";
    html += "<div class='field-item'><label for='useLightSensor'>Use Light Sensor:</label><input type='checkbox' id='useLightSensor' name='useLightSensor' " + String(prefsData.useLightSensor ? "checked" : "") + "></div>";
    html += "<p> All Below Settings Require Sensor</p>";
    html += "<div class='field-item'><label for='nightDisplayOff'>Display Off At Night:</label><input type='checkbox' id='nightDisplayOff' name='nightDisplayOff' " + String(prefsData.nightDisplayOff ? "checked" : "") + "></div>";
    html += "<div class='field-item'><label for='nightDisplayOffTime'>Display Off Hour (22):</label><input type='number' id='nightDisplayOffTime' name='nightDisplayOffTime' value='" + String(prefsData.nightDisplayOffTime) + "' min='0' max='23'></div>";  
    html += "<div class='field-item'><label for='brightnessSlope'>Brightness Slope:</label><input type='number' id='brightnessSlope' name='brightnessSlope' step='any' value='" + String(prefsData.brightnessSlope) + "'></div>";
    html += "<p><b>0.25</b>, Large: Steeper, Small: Flatter</p>";
    html += "<div class='field-item'><label for='brightnessLateral'>Brightness L R:</label><input type='number' id='brightnessLateral' name='brightnessLateral' value='" + String(prefsData.brightnessLateral) + "' min='-128' max='127'></div>";
    html += "<p><b>0</b>, Y Intercept: + shifts Left, - right</p>";
    html += "<div class='field-item'><label for='brightnessMax'>Max LED Brightness:</label><input type='number' id='brightnessMax' name='brightnessMax' value='" + String(prefsData.brightnessMax) + "' min='0' max='255'></div>";
    html += "<div class='field-item'><label for='brightnessMin'>Min LED Brightness:</label><input type='number' id='brightnessMin' name='brightnessMin' value='" + String(prefsData.brightnessMin) + "' min='0' max='255'></div>";
    html += "<div class='field-item'><label for='brightnessMin'>Min DARK Brightness:</label><input type='number' id='brightnessMinDark' name='brightnessMinDark' value='" + String(prefsData.brightnessMinDark) + "' min='0' max='255'></div>";
    html += "<p>Bright = 0 - 255</p>";
    html += "<div class='field-item'><label for='luxDeltaCovered'>Delta Light:</label><input type='number' id='luxDeltaCovered' name='luxDeltaCovered' value='" + String(prefsData.luxDeltaCovered) + "' min='0' max='100'></div>";
    html += "<p><b>50 </b>, &#37; sensor coverage = press</p>"; // &#37; = %
    html += "<div class='field-item'><label for='luxDarkMode'>Dark Mode:</label><input type='number' id='luxDarkMode' name='luxDarkMode' value='" + String(prefsData.luxDarkMode) + "' min='0' max='255'></div>";
    html += "<p><b>4</b>, Sensor Value for Dark</p>";
    html += "<div class='field-item'><label for='luxStabilityVariance'>Stability Variance:</label><input type='number' id='luxStabilityVariance' name='luxStabilityVariance' step='any' value='" + String(prefsData.luxStabilityVariance) + "'></div>";
    html += "<p><b>1.75</b>, &#37; dif sample for light stable.</p>"; // &#37; = %
    
    html += "<input type='submit' value='Save Values'>";
    html += "</form>";
    html += "<h2 class='home-link'><a href='/'>Back to Home</a></h2>";
    html += "</div>";
    html += HTML_FOOTER;
    server.send(200, "text/html", html);
}
void handleNamespacePage(int nsIndex) {
    String html = HTML_PARTA;
    html += "DojoClock-Program";
    html += HTML_PARTB;
    html += "Program " + String(nsIndex) + "</h1>";
    html += "<form action='/submit_program" + String(nsIndex) + "' method='POST'>";

    // Input for the title of the program
    html += "<div class='field-row'><div class='field-item'><label for='title'>Title:</label><input type='text' id='title' name='title' value='" + namespaces[nsIndex].title + "' maxlength='7'></div></div>";

    // Loop through pairs of char and byte elements for display and input
    for (int j = 0; j < NUM_PAIR_ELEMENTS; ++j) {
        html += "<div class='field-row'>";
        // Char element display and input
        html += "<div class='field-item'>";
        html += "<label>" + String(j+1) + "</label>";
        html += "<input type='textchar' name='" + String(j * 2) + "' value='" + String(namespaces[nsIndex].charElements[j]) + "' maxlength='1'>"; //Max  1 char length
        html += "</div>";

        // Byte element display and input
        html += "<div class='field-item'>";
        html += "<input type='textbyte' name='" + String(j * 2 + 1) + "' value='" + String(namespaces[nsIndex].byteElements[j]) + "' maxlength='3'>"; // Max 3 digits for byte (0-255)
        html += "</div>";
        html += "</div>"; // End field-row for this pair
    }

    html += "<input type='submit' value='Save Values'>";
    html += "</form>";
    html += "<h2 class='home-link'><a href='/'>Back to Home</a></h2>";
    html += "</div>";
    html += HTML_FOOTER;
    server.send(200, "text/html", html);
}
void handleSubmitNamespace(int nsIndex) {
    String html = HTML_PARTA;
    html += "DojoClock-Submitted";
    html += HTML_PARTB;
    html += "Submision Results</h1>";

    String nsName = String(nsIndex); // Namespace name (e.g., "0")
    eeprom.begin(nsName.c_str(), false);
    bool changed = false;

    // Update title if submitted and changed
    if (server.hasArg("title")) {
        String newTitle = server.arg("title");
        if (newTitle != namespaces[nsIndex].title) {
            eeprom.putString("title", newTitle);
            namespaces[nsIndex].title = newTitle;
            html += "<p>Title updated to: " + newTitle + "</p>";
            changed = true;
        }
    }

    // Update elements (char and byte)
    for (int j = 0; j < TOTAL_NAMESPACE_ELEMENTS; ++j) {
        String key = String(j); // The name of the input field (e.g., "0", "1", "2"...)
        if (server.hasArg(key)) {
            String newValueStr = server.arg(key);

            if (j % 2 == 0) { // Even index: a character element
                int charArrIdx = j / 2; // Index within the charElements array (0-29)
                char newChar = newValueStr.length() > 0 ? newValueStr.charAt(0) : ' '; // Get first char, or space if empty
                if (newChar != namespaces[nsIndex].charElements[charArrIdx]) {
                    eeprom.putChar(key.c_str(), newChar);
                    namespaces[nsIndex].charElements[charArrIdx] = newChar;
                    html += "<p>Program " + nsName + "[" + key + "] (char) updated to: '" + String(newChar) + "'</p>";
                    changed = true;
                }
            } else { // Odd index: a byte element
                int byteArrIdx = (j - 1) / 2; // Index within the byteElements array (0-29)
                // Convert string to long first to handle potential out-of-range input gracefully
                long tempValue = newValueStr.toInt();
                uint8_t newByte;
                if (tempValue >= 0 && tempValue <= 255) {
                    newByte = (uint8_t)tempValue;
                } else {
                    newByte = 0; // Default to 0 if input is invalid or out of byte range
                    html += "<p>Warning: Invalid byte value for Program " + nsName + "[" + key + "]. Input '" + newValueStr + "' defaulted to 0.</p>";
                }

                if (newByte != namespaces[nsIndex].byteElements[byteArrIdx]) {
                    eeprom.putUChar(key.c_str(), newByte); // Use putUChar for byte type
                    namespaces[nsIndex].byteElements[byteArrIdx] = newByte;
                    html += "<p>Program " + nsName + "[" + key + "] (byte) updated to: " + String(newByte) + "</p>";
                    changed = true;
                }
            }
        }
    }
    eeprom.end();

    if (!changed) {
        html += "<p>No changes detected for this program.</p>";
    }

    html += "<h2 class='home-link'><a href='/'>Back to Home</a></h2>";
    html += "</div>";
    html += HTML_FOOTER;
    server.send(200, "text/html", html);
}
void lcdFooter(){
  if (deviceMode == 2){
    lcd.setCursor(0,3);
    lcd.print("ANY-Pause, STOP-Exit");
  }

  if (deviceMode == 111||deviceMode == 112|| deviceMode == 113||deviceMode == 121||deviceMode == 131||deviceMode == 132||deviceMode == 133||deviceMode == 134){
    lcd.setCursor(0,3);
    lcd.print("START-Save,STOP-Exit");
  }
}
void lcdSetup(){ // Mode 1: Clock, 2: CounterUP, 3: Manual Entry, 4: Program Running, 5:Program Select, 6: Setup Select, 11-16: Setup Pages
  if (deviceMode == 0 || deviceMode == 14 || deviceMode == 15){ // TERMINAL MODE  0: Boot Up, 14:   , 15: Sensor Data
    terminal.refresh();
    terminal.clear();
    //lcd.clear();
    //lcd.setCursor(0,0); //Char 0 , Row 0
    lcd.noCursor();
    lcd.noBlink();
    //lcdRow = 0;
  }
  if (deviceMode == 1 || deviceMode == 255){ // CLOCK MODE
    terminal.clear();
    lcd.clear();
    lcd.noCursor();
    lcd.noBlink();
    lcd.setCursor(0,0); //Char 0 , Row 0
    if (worldTime){lcd.print(&timeinfo, "%H:%M:%S");} else {lcd.print(&timeinfo, "%I:%M:%S");} //%p am/pm
    lcd.setCursor(10,0);
    lcd.print("F1-");
    lcd.print(namespaces[1].title);
    lcd.setCursor(0,1);
    //lcd.print(timeZone);
    //if (DST){lcd.print("DST");}else{lcd.print("ST");}
    lcd.print(&timeinfo, "%Z"); // Current Timezone from clock
    lcd.setCursor(5,1);
    if (!worldTime){lcd.print(&timeinfo, "  %p");}
    lcd.setCursor(10,1);
    lcd.print("F2-");
    lcd.print(namespaces[2].title);
    lcd.setCursor(0,2);
    lcd.print(&timeinfo, "%h %d,%y");
    lcd.setCursor(10,2);
    lcd.print("F3-");
    lcd.print(namespaces[3].title);
    lcd.setCursor(0,3);
    lcd.print(&timeinfo, "%a  ");
    //lcd.write(0xDF); // Degrees
    if (wifiSuccess) {lcd.write(0);} // Print the custom character at index 0 Wifi
    if (NTPSuccess) {
      lcd.setCursor(8,3);
      lcd.write(1); // Clock Symbol
    }
    lcd.setCursor(10,3);
    lcd.print("F4- More");
  }
  if (deviceMode == 2){ //2: Count Up Running
    lcd.clear();
    lcd.noCursor();
    lcd.noBlink();
    lcd.setCursor(6,0);
    lcd.print("TIMER UP");
    lcdFooter();
  }
  if (deviceMode == 3){
  // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    if (useBonus == 0 && useRest == 0 && useRounds == 0){
    lcd.clear();
    lcd.cursor();
    lcd.blink();
    lcd.setCursor(0,0);
    lcd.print("FIGHT");
    //lcd.print(fightRegister[0] + fightRegister[1] + ":" + fightRegister[2] + fightRegister[3]);
    lcd.setCursor(11,0);
    lcd.print("F1-BONUS%");
    lcd.setCursor(11,1);
    lcd.print("F2-BLINDB");
    lcd.setCursor(11,2);
    lcd.print("F3-REST");
    lcd.setCursor(11,3);
    lcd.print("F4-ROUNDS");
    }
    if (useBonus == 0 ){
      lcd.setCursor(0,1);
      lcd.print("           ");
    }
    if (useRest == 0){
      lcd.setCursor(0,2);
      lcd.print("          ");
    }
    if (useRounds == 0){
      lcd.setCursor(0,3);
      lcd.print("          ");
    }
  }
  if (deviceMode == 31){
  // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    lcd.setCursor(0,1);
    lcd.print("BONUS ");
  }
  if (deviceMode == 32){
  // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    lcd.setCursor(0,2);
    lcd.print("REST ");
  }
  if (deviceMode == 33){
  // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
  if (useRounds == 1){
    lcd.setCursor(0,3);
    lcd.print("ROUNDS ");
    }
  }
  if (deviceMode == 4){ //4: Program Running
    terminal.clear();
    lcd.clear();
    lcd.noCursor();
    lcd.noBlink();
    lcd.setCursor(0,0);
    lcd.print(progNum);
    lcd.print("-");
    lcd.print(namespaces[progNum].title);
    if (useFight){
      lcd.setCursor(0,1);
      lcd.print("FIGHT ");
      lcd.print(fightRegister[0]);
      lcd.print(fightRegister[1]);
      lcd.print(":");
      lcd.print(fightRegister[2]);
      lcd.print(fightRegister[3]);
    }
    if (useBonus == 1){
      lcd.setCursor(0,2);
      lcd.print("BONUS ");
      lcd.print(bonusRegister[0]);
      lcd.print(bonusRegister[1]);
      lcd.print("% ");
      if(blindBonus){ lcd.write(2);} // Blind Symbol
    }
    if (useRest == 1){
      lcd.setCursor(0,3);
      lcd.print("REST ");
      lcd.print(restRegister[0]);
      lcd.print(restRegister[1]);
      lcd.print(":");
      lcd.print(restRegister[2]);
      lcd.print(restRegister[3]);
    }
    if (useRounds == 1){
      lcd.setCursor(12,3);
      lcd.print("ROUND ");
    }
    lcd.setCursor(12,0);
    lcd.print("|");
    lcd.setCursor(12,1);
    lcd.print("|");
    lcd.setCursor(12,2);
    lcd.print("|");
    lcd.setCursor(13,2);
    lcd.print("_______");
  }

  if (deviceMode == 5){ //Program Select Mode
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.noCursor();
    lcd.noBlink();
    lcd.print("4-");
    lcd.print(namespaces[4].title); //Prog 1 title
    lcd.setCursor(10,0);
    lcd.print("8-");
    lcd.print(namespaces[8].title); //Prog 8 title
    lcd.setCursor(0,1);
    lcd.print("5-");
    lcd.print(namespaces[5].title); //Prog 2 title
    lcd.setCursor(10,1);
    lcd.print("9-");
    lcd.print(namespaces[9].title); //Prog 9 title
    lcd.setCursor(0,2);
    lcd.print("6-");
    lcd.print(namespaces[6].title); //Prog 3 title
    lcd.setCursor(10,2);
    lcd.print("0-");
    lcd.print(namespaces[0].title); //Prog 0 title
    lcd.setCursor(0,3);
    lcd.print("7-");
    lcd.print(namespaces[7].title); //Prog 3 title
    lcd.setCursor(10,3);
    lcd.print("F4- SETUP");
  }
  if (deviceMode == 6){ //Setup Options
    lcd.clear();
    lcd.noCursor();
    lcd.noBlink();
    lcd.setCursor(8,0);
    lcd.print("SETUP");
    lcd.setCursor(0,1);
    lcd.print("1-SetDate");
    lcd.setCursor(11,1);
    lcd.print("4-Server");
    lcd.setCursor(0,2);
    lcd.print("2-SetClock");
    lcd.setCursor(11,2);
    lcd.print("5-Sensor");
    lcd.setCursor(0,3);
    lcd.print("3-DSTdates");
    lcd.setCursor(11,3);
    lcd.print("6-Reboot");
  }
  if (deviceMode == 11) {// Setup Page: Date
    lcd.clear();
    lcd.noCursor();
    lcd.noBlink();
    lcd.setCursor(4,0);
    lcd.print (&timeinfo, "%m"); // month
    lcd.setCursor(10,0);
    lcd.print("F1-Month");
    lcd.setCursor(4,1);
    lcd.print(&timeinfo, "%d"); //Day padded with zeros
    lcd.setCursor(10,1);
    lcd.print("F2-Day");
    lcd.setCursor(3,2);
    lcd.print (&timeinfo, "%Y");
    lcd.setCursor(10,2);
    lcd.print("F3-Year");
  }
  if (deviceMode == 111) { // Setup Month Entry Page
    lcd.clear();
    lcd.setCursor(6,0);
    lcd.print("New Month");
    lcdFooter();
    lcd.setCursor(9,1);
    lcd.print(timeRegister[0]);
    lcd.print(timeRegister[1]);
    lcd.setCursor(10,1);
    lcd.cursor();
    lcd.blink();
  }
  if (deviceMode == 112) { // Setup Day Entry Page
    lcd.clear();
    lcd.setCursor(6,0);
    lcd.print("New Day");
    lcdFooter();
    lcd.setCursor(9,1);
    lcd.print(timeRegister[0]);
    lcd.print(timeRegister[1]);
    lcd.setCursor(10,1);
    lcd.cursor();
    lcd.blink();
  }
  if (deviceMode == 113){ // Setup Year Entry Page
    lcd.clear();
    lcd.setCursor(6,0);
    lcd.print("New Year");
    lcdFooter();
    lcd.setCursor(8,1);
    lcd.print(timeRegister[0]);
    lcd.print(timeRegister[1]);
    lcd.print(timeRegister[2]);
    lcd.print(timeRegister[3]);
    lcd.setCursor(11,1);
    lcd.cursor();
    lcd.blink();
  }
  if (deviceMode == 12) { // Clock Settings Page
    lcd.clear();
    lcd.noCursor();
    lcd.noBlink();
    lcd.setCursor(2,0);
    //lcd.print(timeZone); // Letter ONLY
    switch (timeZone) {
      case 'E':
        lcd.print("East");
        break;
      case 'C':
        lcd.print("Cent");
        break;
      case 'M': 
        lcd.print("Moun");
        break;  
      case 'W':
        lcd.print("West");
        break;
      default:
      break;
    }
    lcd.setCursor(10,0);
    lcd.print("F1-tZone");
    lcd.setCursor(3,1);
    if (useDST){lcd.print("Yes");} else{lcd.print("No");}
    lcd.setCursor(10,1);
    lcd.print("F2-Use DST");
    lcd.setCursor(2,2);
    lcd.print (&timeinfo, "%R"); // HH:MM This includes a leading space!?
    lcd.setCursor(10,2);
    lcd.print("F3-Time");
    lcd.setCursor(1,3);
    if (worldTime){lcd.print("24 Hour");} else lcd.print("12 Hour");
    lcd.setCursor(10,3);
    lcd.print("F4-12/24");
  }
  if (deviceMode == 121) { // Setup Clock Time Entry Page
    lcd.clear();
    lcd.setCursor(3,0);
    lcd.print("New 24 Hr Time");
    lcdFooter();
    lcd.setCursor(7,1);
    lcd.print(timeRegister[0]);
    lcd.print(timeRegister[1]);
    lcd.print(":");
    lcd.print(timeRegister[2]);
    lcd.print(timeRegister[3]);
    lcd.setCursor(11,1);
    lcd.cursor();
    lcd.blink();
  }
  if (deviceMode == 13){ // Setup DST Dates
    lcd.clear();
    lcd.noCursor();
    lcd.noBlink();
    lcd.setCursor(0,0);
    lcd.print("Start [");
    lcd.print(DSTStartSunday);
    lcd.print("]F1 Sunday");
    lcd.setCursor(0,1);
    lcd.print("of month [");
    lcd.print(DSTStartMonth);
    lcd.print("]F2.");
    lcd.setCursor(0,2);
    lcd.print("End [");
    lcd.print(DSTEndSunday);
    lcd.print("]F3 Sunday of");
    lcd.setCursor(0,3);
    lcd.print("month [");
    lcd.print(DSTEndMonth);
    lcd.print("]F4.");
  }
  if (deviceMode == 131) { // Start Sunday Entry
    lcd.clear();
    lcd.setCursor(2,0);
    lcd.print("DST Start Sunday #");
    lcd.setCursor(3,1);
    lcd.print("Default: 2nd");
    lcdFooter();
    lcd.setCursor(9,2);
    lcd.print(timeRegister[0]);
    lcd.setCursor(9,2);
    lcd.cursor();
    lcd.blink();
  }
  if (deviceMode == 132) { // Start Month Entry
    lcd.clear();
    lcd.setCursor(1,0);
    lcd.print("DST Start Month #");
    lcd.setCursor(2,1);
    lcd.print("Default: 03 March");
    lcdFooter();
    lcd.setCursor(9,2);
    lcd.print(timeRegister[0]);
    lcd.print(timeRegister[1]);
    lcd.setCursor(10,2);
    lcd.cursor();
    lcd.blink();
  }
  if (deviceMode == 133) { // End Sunday Entry
    lcd.clear();
    lcd.setCursor(2,0);
    lcd.print("DST End Sunday #");
    lcd.setCursor(4,1);
    lcd.print("Default: 1st");
    lcdFooter();
    lcd.setCursor(9,2);
    lcd.print(timeRegister[0]);
    lcd.setCursor(9,2);
    lcd.cursor();
    lcd.blink();
  }
  if (deviceMode == 134) { // End Month Entry
    lcd.clear();
    lcd.setCursor(2,0);
    lcd.print("DST End Month #");
    lcd.setCursor(2,1);
    lcd.print("Default: 11 Nov");
    lcdFooter();
    lcd.setCursor(9,2);
    lcd.print(timeRegister[0]);
    lcd.print(timeRegister[1]);
    lcd.setCursor(10,2);
    lcd.cursor();
    lcd.blink();
  }
  lcdMode = deviceMode; // Last line after LCD setup complete.
}
void lcdRefresh(){
  if (lcdMode != deviceMode){ lcdSetup(); }
  
  if (deviceMode == 1 || deviceMode == 255){ // CLOCK MODE
    lcd.setCursor(0,0); //Char 0 , Row 0
    if (worldTime){lcd.print(&timeinfo, "%H:%M:%S");} else {lcd.print(&timeinfo, "%I:%M:%S");} //&p am/pm
  }
  if (deviceMode == 2){ // Count Up
    lcd.setCursor(6,1); //Char 0 , Row 0
    uint8_t tempByte = timerU2.ShowHours();
    if (tempByte < 10) {
      lcd.print("0");
      lcd.print(tempByte);
    }
    else {lcd.print(tempByte);}
    lcd.print(":");
    tempByte = timerU2.ShowMinutes();
    if (tempByte < 10) {
      lcd.print("0");
      lcd.print(tempByte);
    }
    else {lcd.print(tempByte);}
    lcd.print(":");
    tempByte = timerU2.ShowSeconds();
    if (tempByte < 10) {
      lcd.print("0");
      lcd.print(tempByte);
    }
    else {lcd.print(tempByte);}
  }
  if (deviceMode == 3){
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    if(blindBonus == 1 && useBonus ==1){ lcd.setCursor(9,1); lcd.write(2);} else { lcd.setCursor(9,1); lcd.print(" "); }
    lcd.setCursor(5,0);
    lcd.print(fightRegister[0]);
    lcd.print(fightRegister[1]);
    lcd.print(":");
    lcd.print(fightRegister[2]);
    lcd.print(fightRegister[3]);
    lcd.setCursor(9,0);
  }
  if (deviceMode == 31){
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    if(blindBonus == 1 && useBonus ==1){ lcd.setCursor(9,1); lcd.write(2);} else { lcd.setCursor(9,1); lcd.print(" "); }
    lcd.setCursor(6,1);
    lcd.print(bonusRegister[0]);
    lcd.print(bonusRegister[1]);
    lcd.setCursor(7,1);
  }
  if (deviceMode == 32){
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    if(blindBonus == 1 && useBonus ==1){ lcd.setCursor(9,1); lcd.write(2);} else { lcd.setCursor(9,1); lcd.print(" "); }
    lcd.setCursor(5,2);
    lcd.print(restRegister[0]);
    lcd.print(restRegister[1]);
    lcd.print(":");
    lcd.print(restRegister[2]);
    lcd.print(restRegister[3]);
    lcd.setCursor(9,2);
  }
  if (deviceMode == 33){
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    if(blindBonus == 1 && useBonus ==1){ lcd.setCursor(9,1); lcd.write(2);} else { lcd.setCursor(9,1); lcd.print(" "); }
    lcd.setCursor(7,3);
    lcd.print(roundsRegister[0]);
    lcd.print(roundsRegister[1]);
    lcd.setCursor(8,3);
  }
  if (deviceMode == 4){ //4: Program Running
    lcd.setCursor(15,0);
    if (sourceT == 0){if (worldTime){lcd.print(&timeinfo, "%H:%M");} else {lcd.print(&timeinfo, "%I:%M");} } // Source is Clock.
    //if (sourceT == 0){if (worldTime){lcd.print(&timeinfo, "%H:%M:%S");} else {lcd.print(&timeinfo, "%I:%M:%S");} } // Source is Clock.

    if (sourceT == 1){ // Source is timerD1
      uint8_t tempByte = timerD1.ShowMinutes();
      if (tempByte < 10) {
        lcd.print("0");
        lcd.print(tempByte);
      }
      else {lcd.print(tempByte);}
      lcd.print(":");
      tempByte = timerD1.ShowSeconds();
      if (tempByte < 10) {
        lcd.print("0");
        lcd.print(tempByte);
      }
      else {lcd.print(tempByte);}
    }

    if (sourceT == 2){ // Source is timerU2
      uint8_t tempByte = timerU2.ShowMinutes();
      if (tempByte < 10) {
        lcd.print("0");
        lcd.print(tempByte);
      }
      else {lcd.print(tempByte);}
      lcd.print(":");
      tempByte = timerU2.ShowSeconds();
      if (tempByte < 10) {
        lcd.print("0");
        lcd.print(tempByte);
      }
      else {lcd.print(tempByte);}
    }

    if (sourceT == 3){ // Source is timerD3
      uint8_t tempByte = timerD3.ShowMinutes();
      if (tempByte < 10) {
        lcd.print("0");
        lcd.print(tempByte);
      }
      else {lcd.print(tempByte);}
      lcd.print(":");
      tempByte = timerD3.ShowSeconds();
      if (tempByte < 10) {
        lcd.print("0");
        lcd.print(tempByte);
      }
      else {lcd.print(tempByte);}
    }

    if (useRounds){
      lcd.setCursor(18,3);
      lcd.print(roundsRegister[0]);
      lcd.print(roundsRegister[1]);
    }
  }    
  if (deviceMode == 12) { // Clock Settings Page
    lcd.setCursor(2,2);
    lcd.print (&timeinfo, "%R"); // HH:MM This includes a leading space!?
  }
  if (deviceMode == 15){ //Settings-Sensor Output Page
    terminal.print(lux);
    terminal.print("--");
    terminal.print(luxRoomValid);
    terminal.print("--");
    terminal.println(brightnessTarget);
  }
}
void fastLEDRefresh(){
  // D0:0-40(41), D1:41-81(41), D2:84-124(41), D3:125-165(41), D4:166-203(38), D5:204-241(38), D6:242-324(83), D7:325-407(83), D8:410-492(83), D9:493-575(83)
  // LC:82-83(2), UC:408-409(2)
  // U(106) = 242-575(334), V(103) = 0-165(166), W(104) = 166-241(76)
  if (paintEntire){
    // -----------ALL (255)
    // if (color255 > 14 ){ color255 = 13; } //error checking set to white as default.
    if (color255 <= 13){ solidColorRange(0, 575, color255); }
    if (color255 == 14){ rainbowRangeBlend(0, 575, 7); } //rainbowRange(int startIndex, int endIndex, uint8_t spacing)
    // color == 15 is Random solid color
    // color255 = Effect # + 15
    if (color255 == 16){ confettiPops(); }        // #1
    if (color255 == 17){ confettiClusters(); }    // #2
    if (color255 == 18){ confettiDrift(); }       // #3
    if (color255 == 19){ confettiParty(); }       // #4
    if (color255 == 20){ sinelonWide(); }         // #5 cool
    if (color255 == 21){ juggle(); }              // #6
    if (color255 == 22){ bpm(); }                 // #7 pulsing w/ rainbow colors
                  //23                            // #8 EiffelSparkle Random Color using 27-35 [color255 values]
                  //24                            // #9 EiffelSparkle MAX Random Color using 36-44
                  //25                            // #10 SloshColors Random using 45-80
                  //26                            // #11 bpm 2 random colors 81-112
    // Eiffel Sparkle Colors
    if (color255 == 27){ eiffelSparkle(5,  1); }
    if (color255 == 28){ eiffelSparkle(5,  2); }
    if (color255 == 29){ eiffelSparkle(5,  3); }
    if (color255 == 30){ eiffelSparkle(5,  4); }
    if (color255 == 31){ eiffelSparkle(5,  7); }
    if (color255 == 32){ eiffelSparkle(5,  8); }
    if (color255 == 33){ eiffelSparkle(5, 10); }
    if (color255 == 34){ eiffelSparkle(5, 12); }
    if (color255 == 35){ eiffelSparkle(5, 13); }

    // Eiffel Sparkle Max Colors
    if (color255 == 36){ eiffelSparkle(20,  1); }
    if (color255 == 37){ eiffelSparkle(20,  2); }
    if (color255 == 38){ eiffelSparkle(20,  3); }
    if (color255 == 39){ eiffelSparkle(20,  4); }
    if (color255 == 40){ eiffelSparkle(20,  7); }
    if (color255 == 41){ eiffelSparkle(20,  8); }
    if (color255 == 42){ eiffelSparkle(20, 10); }
    if (color255 == 43){ eiffelSparkle(20, 12); }
    if (color255 == 44){ eiffelSparkle(20, 13); }

    // Slosh Colors Red w/
    if (color255 == 45){ sloshColors(140, 1, 2); }
    if (color255 == 46){ sloshColors(140, 1, 3); }
    if (color255 == 47){ sloshColors(140, 1, 4); }
    if (color255 == 48){ sloshColors(140, 1, 7); }
    if (color255 == 49){ sloshColors(140, 1, 8); }
    if (color255 == 50){ sloshColors(140, 1, 10); }
    if (color255 == 51){ sloshColors(140, 1, 12); }
    if (color255 == 52){ sloshColors(140, 1, 13); }

    // Slosh Colors Orange w/
    if (color255 == 53){ sloshColors(140, 2, 3); }
    if (color255 == 54){ sloshColors(140, 2, 4); }
    if (color255 == 55){ sloshColors(140, 2, 7); }
    if (color255 == 56){ sloshColors(140, 2, 8); }
    if (color255 == 57){ sloshColors(140, 2, 10); }
    if (color255 == 58){ sloshColors(140, 2, 12); }
    if (color255 == 59){ sloshColors(140, 2, 13); }

    // Slosh Colors Yellow w/
    if (color255 == 60){ sloshColors(140, 3, 4); }
    if (color255 == 61){ sloshColors(140, 3, 7); }
    if (color255 == 62){ sloshColors(140, 3, 8); }
    if (color255 == 63){ sloshColors(140, 3, 10); }
    if (color255 == 64){ sloshColors(140, 3, 12); }
    if (color255 == 65){ sloshColors(140, 3, 13); }

    // Slosh Colors Green w/
    if (color255 == 66){ sloshColors(140, 4, 7); }
    if (color255 == 67){ sloshColors(140, 4, 8); }
    if (color255 == 68){ sloshColors(140, 4, 10); }
    if (color255 == 69){ sloshColors(140, 4, 12); }
    if (color255 == 70){ sloshColors(140, 4, 13); }

    // Slosh Colors Cyan w/
    if (color255 == 71){ sloshColors(140, 7, 8); }
    if (color255 == 72){ sloshColors(140, 7, 10); }
    if (color255 == 73){ sloshColors(140, 7, 12); }
    if (color255 == 74){ sloshColors(140, 7, 13); }

    // Slosh Colors Blue w/
    if (color255 == 75){ sloshColors(140, 8, 10); }
    if (color255 == 76){ sloshColors(140, 8, 12); }
    if (color255 == 77){ sloshColors(140, 8, 13); }

    // Slosh Colors Purple w/
    if (color255 == 78){ sloshColors(140, 10, 12); }
    if (color255 == 79){ sloshColors(140, 10, 13); }

    // Slosh Colors Pink w/
    if (color255 == 80){ sloshColors(140, 12, 13); }

    // BPM 2 Colors Red w/
    if (color255 == 81){ bpm_hsv(0,255,21,255);}   // Orange
    if (color255 == 82){ bpm_hsv(0,255,42,255);}   // Yellow
    if (color255 == 83){ bpm_hsv(0,255,85,255);}   // Green
    if (color255 == 84){ bpm_hsv(0,255,170,255);}  // Blue
    if (color255 == 85){ bpm_hsv(0,255,191,255);}  // Purple
    if (color255 == 86){ bpm_hsv(0,255,232,178);}  // Pink
    if (color255 == 87){ bpm_hsv(0,255,0,0);}      // White

    // BPM 2 Colors Orange w/
    if (color255 == 88){ bpm_hsv(21,255,42,255);}   // Yellow
    if (color255 == 89){ bpm_hsv(21,255,85,255);}   // Green
    if (color255 == 90){ bpm_hsv(21,255,170,255);}  // Blue
    if (color255 == 91){ bpm_hsv(21,255,191,255);}  // Purple
    if (color255 == 92){ bpm_hsv(21,255,232,178);}  // Pink
    if (color255 == 93){ bpm_hsv(21,255,0,0);}      // White

    // BPM 2 Colors Yellow w/
    if (color255 == 94){ bpm_hsv(42,255,85,255);}   // Green
    if (color255 == 95){ bpm_hsv(42,255,170,255);}  // Blue
    if (color255 == 96){ bpm_hsv(42,255,191,255);}  // Purple
    if (color255 == 97){ bpm_hsv(42,255,232,178);}  // Pink
    if (color255 == 98){ bpm_hsv(42,255,0,0);}      // White

    // BPM 2 Colors Green w/
    if (color255 == 99){ bpm_hsv(85,255,170,255);}  // Blue
    if (color255 == 100){ bpm_hsv(85,255,191,255);}  // Purple
    if (color255 == 101){ bpm_hsv(85,255,232,178);}  // Pink
    if (color255 == 102){ bpm_hsv(85,255,0,0);}      // White

    // BPM 2 Colors Blue w/
    if (color255 == 103){ bpm_hsv(170,255,191,255);}  // Purple
    if (color255 == 104){ bpm_hsv(170,255,232,178);}  // Pink
    if (color255 == 105){ bpm_hsv(170,255,0,0);}      // White

    // BPM 2 Colors Purple w/
    if (color255 == 106){ bpm_hsv(191,255,232,178);}  // Pink
    if (color255 == 107){ bpm_hsv(191,255,0,0);}      // White

    // BPM 2 Colors Pink w/
    if (color255 == 108){ bpm_hsv(232,178,0,0);}      // White


//  0: Black
//  1: Red
//  2: Orange
//  3: Yellow
//  4: Green
//  5: Dark Green
//  6: Teal
//  7: Cyan
//  8: Blue
//  9: Indigo
//  10: Purple
//  11: Magenta
//  12: Pink
//  13: White
//  14: Rainbow

//  const uint8_t Hue[14] = {
//      0,   // Black
//      0,   // Red
//      21,  // Orange
//      42,  // Yellow
//      85,  // Green
//      85,  // Dark Green
//      127, // Teal
//      127, // Cyan
//      170, // Blue
//      191, // Indigo
//      191, // Purple
//      213, // Magenta
//      232, // Pink
//      0    // White
//  };
//  
//  const uint8_t Sat[14] = {
//      0,   // Black
//      255, // Red
//      255, // Orange
//      255, // Yellow
//      255, // Green
//      255, // Dark Green
//      255, // Teal
//      255, // Cyan
//      255, // Blue
//      255, // Indigo
//      255, // Purple
//      255, // Magenta
//      178, // Pink
//      0    // White
//  };
//  
//  const uint8_t Val[14] = {
//      0,   // Black
//      255, // Red
//      240, // Orange
//      255, // Yellow
//      255, // Green
//      110, // Dark Green
//      110, // Teal
//      240, // Cyan
//      255, // Blue
//      115, // Indigo
//      110, // Purple
//      240, // Magenta
//      220, // Pink
//      245  // White
//  };
//  


  }
  if (!paintEntire) {
    if (split100_102){ // Yes, paint Lower Right (100, LC, 102) as seperates!
      // -----------Section (100) D0-1, Lower Right-right ------------------------------
      if (color100 >  16){ color100 = 13; } //error checking set to white as default.
      if (color100 <= 13){ solidColorRange(0, 81, color100); }
      if (color100 == 14){ rainbowRangeBlend(0, 81, 7); }
      // -----------Section (LC). Lower Colon
      if (colorLC >  16){ colorLC = 13; } //error checking set to white as default.
      if (colorLC <= 13){ solidColorRange(82, 83, colorLC); }
      if (colorLC == 14){ rainbowRangeBlend(82, 83, 7); }
      // -----------Section (102) D2-3. Lower Right-left
      if (color102 >  16) { color102 = 13; } //error checking set to white as default.
      if (color102 <= 13) { solidColorRange(84, 165, color102); }
      if (color102 == 14) { rainbowRangeBlend(84, 165, 7); }
    } else { // -----------NO, paint Lower Right (100, LC, 102) AS ONE V(103)!
      if (color103 >  16) { color103 = 13; } //error checking set to white as default.
      if (color103 <= 13) { solidColorRange(0, 165, color103); }
      if (color103 == 14) { rainbowRangeBlend(0, 165, 7); }
    }
    // -----------Section W(104) D4-5, Lower Left
    if (color104 >  16) { color104 = 13; } //error checking set to white as default.
    if (color104 <= 13) { solidColorRange(166, 241, color104); }
    if (color104 == 14) { rainbowRangeBlend(166, 241, 7); }
    // -----------Section U(106) D6-9 Upper Large
    if (color106 > 16 ) { color106 = 13;} //error checking set to white as default.
    if (color106 <= 13) { solidColorRange(242, 575, color106); }
    if (color106 == 14) { rainbowRangeBlend(242, 575, 7); }
    // if (color106 == 16){ confettiRange(242, 575 ); }
  }

  if (useLedDigitMask){ // Apply digit masks for numbers to display.
    for (uint8_t j = 0; j < 10; j++){ //Address to send current digit green "1"
      maskDigit(j,ledDigit[j]);
      if (!upperColon){
        leds[408] = CRGB::Black;
        leds[409] = CRGB::Black;
      }
      if (!lowerColon){
        leds[82] = CRGB::Black;
        leds[83] = CRGB::Black;
      }
    }
  }
  FastLED.show();    // send the 'leds' array out to the actual LED strip
  //if (runningFastEffect){FastLED.delay(1000/FRAMES_PER_SECOND);}  // insert a delay to keep the framerate modest}
  FastLED.delay(1000/FRAMES_PER_SECOND);
  EVERY_N_MILLISECONDS( 20 ) { gHue++; }
  //}
}
void ledClockUpper(){         // U Source = 0
  uint8_t tempByte;
  if (worldTime){
    tempByte = timeinfo.tm_hour;
    ledDigit[9] = tempByte / 10; // First Digit
    if (ledDigit[9] == 0){ledDigit[9] = 10;} // 10 is full mask
    ledDigit[8] = tempByte % 10; // Second Digit
  } 
  else {   // AM/PM
    tempByte = timeinfo.tm_hour;
    if (tempByte == 0){ledDigit[9] = 1; ledDigit[8] = 2;}
    else {
      if (tempByte > 12){tempByte = tempByte -12;}
      ledDigit[9] = tempByte / 10; // First Digit
      if (ledDigit[9] == 0){ledDigit[9] = 10;} // 10 is full mask
      ledDigit[8] = tempByte % 10; // Second Digit
    }
  }
  tempByte = timeinfo.tm_min;
  ledDigit[7] = tempByte / 10; // First Digit
  ledDigit[6] = tempByte % 10; // Second Digit
}
void ledTimerD1Upper(){       // U Source = 1
  uint8_t tempByte = timerD1.ShowMinutes();
  if (tempByte < 10) {
    ledDigit[9] = 0;
    ledDigit[8] = tempByte;
  }
  else {
    ledDigit[9] = tempByte / 10; // First Digit
    ledDigit[8] = tempByte % 10; // Second Digit
  }
  tempByte = timerD1.ShowSeconds();
  if (tempByte < 10) {
    ledDigit[7] = 0;
    ledDigit[6] = tempByte;
  }
  else {
    ledDigit[7] = tempByte / 10; // First Digit
    ledDigit[6] = tempByte % 10; // Second Digit
  }
}
void ledTimerU2Upper(){       // U Source = 2
  uint8_t tempByte = timerU2.ShowMinutes();
  if (tempByte < 10) {
    ledDigit[9] = 0;
    ledDigit[8] = tempByte;
  }
  else {
    ledDigit[9] = tempByte / 10; // First Digit
    ledDigit[8] = tempByte % 10; // Second Digit
  }
  tempByte = timerU2.ShowSeconds();
  if (tempByte < 10) {
    ledDigit[7] = 0;
    ledDigit[6] = tempByte;
  }
  else {
    ledDigit[7] = tempByte / 10; // First Digit
    ledDigit[6] = tempByte % 10; // Second Digit
  }
}
void ledClockLowerRight(){    // V Source = 0
  uint8_t tempByte;
  if (worldTime){
    tempByte = timeinfo.tm_hour;
    ledDigit[3] = tempByte / 10; // First Digit
    if (ledDigit[3] == 0){ledDigit[3] = 10;} // 10 is full mask
    ledDigit[2] = tempByte % 10; // Second Digit
  } 
  else {   // AM/PM
    tempByte = timeinfo.tm_hour;
    if (tempByte > 12){tempByte = tempByte -12;}
    ledDigit[3] = tempByte / 10; // First Digit
    if (ledDigit[3] == 0){ledDigit[3] = 10;} // 10 is full mask
      ledDigit[2] = tempByte % 10; // Second Digit
    }
  tempByte = timeinfo.tm_min;
  ledDigit[1] = tempByte / 10; // First Digit
  ledDigit[0] = tempByte % 10; // Second Digit
}
void ledTimerD1LowerRight(){  // V Source = 1
  uint8_t tempByte = timerD1.ShowMinutes();
  if (tempByte < 10) {
    ledDigit[3] = 0;
    ledDigit[2] = tempByte;
  }
  else {
    ledDigit[3] = tempByte / 10; // First Digit
    ledDigit[2] = tempByte % 10; // Second Digit
  }
  tempByte = timerD1.ShowSeconds();
  if (tempByte < 10) {
    ledDigit[1] = 0;
    ledDigit[0] = tempByte;
  }
  else {
    ledDigit[1] = tempByte / 10; // First Digit
    ledDigit[0] = tempByte % 10; // Second Digit
  }
}
void ledTimerU2LowerRight(){  // V Source = 2
  uint8_t tempByte = timerU2.ShowMinutes();
  if (tempByte < 10) {
    ledDigit[3] = 0;
    ledDigit[2] = tempByte;
  }
  else {
    ledDigit[3] = tempByte / 10; // First Digit
    ledDigit[2] = tempByte % 10; // Second Digit
  }
  tempByte = timerU2.ShowSeconds();
  if (tempByte < 10) {
    ledDigit[1] = 0;
    ledDigit[0] = tempByte;
  }
  else {
    ledDigit[1] = tempByte / 10; // First Digit
    ledDigit[0] = tempByte % 10; // Second Digit
  }
}
void ledSecLowerLeft(){
  uint8_t tempByte = timeinfo.tm_sec;
  ledDigit[5] = tempByte / 10; // First Digit
  ledDigit[4] = tempByte % 10; // Second Digit
}
void ledTimerU2HoursLowerLeft(){
  uint8_t tempByte = timerU2.ShowHours();
  if (tempByte < 10) {
    ledDigit[5] = 0;
    ledDigit[4] = tempByte;
  }
  else {
    ledDigit[5] = tempByte / 10; // First Digit
    ledDigit[4] = tempByte % 10; // Second Digit
  }
}
void ledSetup(){
  if (deviceMode == 0 ){ // TERMINAL MODE
  }
  if (deviceMode == 1 || deviceMode == 14){ // CLOCK MODE or server mode for color changs to show up right away.
    useLedDigitMask = 1;
    paintEntire = 0;
    split100_102 = 1;
    color100 = prefsData.colorDate; // 3
    lowerColon = 0; // Redundant!! Color Black!
    colorLC = 0;
    color102 = prefsData.colorMonth; // 8
    color104 = prefsData.colorSeconds; // 4
    color106 = prefsData.colorClock; //13 White, 14 Rainbow, 15 Sinelon
    upperColon = 1;
    //sourceU = 0; //Are these needed? cause Can't set Month and Day!
    //sourceV = 0;
    //sourceW = 0;
    // Set Panel onboard LED
    if (NTPSuccess && wifiSuccess){onboardled[0] = CRGB::Blue;} else {onboardled[0] = CRGB::Yellow;}
  }
  if (deviceMode == 2){ // Count Up
    useLedDigitMask = 1;
    paintEntire = 0;
    lowerColon = 1;
    split100_102 = 0;
    color103 = 14;
    color104 = 5;
    color106 = 4;
    upperColon = 1;
    onboardled[0] = CRGB::Green;
  }
  if (deviceMode == 4){ // Program running
    split100_102 = 0;
    sourceV = 0;
    color103 = 0; // sets led displays to black so if they're not used, they're black.
    color104 = 0;
    color106 = 0;
    upperColon = 1;
    lowerColon = 1;
    onboardled[0] = CRGB::Orange;
  }
  if (deviceMode == 255){ // CLOCK DARK MODE
    useLedDigitMask = 1;
    paintEntire = 0;
    split100_102 = 0;
    color103 = 0;
    lowerColon = 0;
    color104 = 0;
    upperColon = 1;
    color106 = 1;
    //sourceU = 0; //Are these needed? cause Can't set Month and Day!
    //sourceV = 0;
    //sourceW = 0;
    // Set Panel onboard LED
    onboardled[0] = CRGB::Black;
  }
  ledMode = deviceMode; // Last line after LCD setup complete.
}
void ledRefresh(){
  if (ledMode != deviceMode){ ledSetup(); }
  if (deviceMode == 0 ){ return; }// BOOT UP TERMINAL MODE, show nothing on LED Display
  if (deviceMode == 2){ // Count Up
    ledClockLowerRight();
    ledTimerU2HoursLowerLeft();
    ledTimerU2Upper();
  }
  if (deviceMode == 4){ // Program Running
    switch (sourceU) {
      case 0:
        ledClockUpper();
        break;
      case 1:
        ledTimerD1Upper();
        break;
      case 2:
        ledTimerU2Upper();
        break;
      default:
        break;
    }
    switch (sourceV) {
      case 0:
        ledClockLowerRight();
        break;
      case 1:
        ledTimerD1LowerRight();
        break;
      case 2:
        ledTimerU2LowerRight();
        break;
      default:
        break;
    }
    switch (sourceW) {
      case 0: // Clock Seconds
        ledSecLowerLeft();
        break;
      case 1:
        ledTimerU2HoursLowerLeft();
        break;
      case 2: // Rounds
        ledDigit[5] = roundsRegister[0];
        ledDigit[4] = roundsRegister[1];
        break;
      default:
        break;
    }
  }
  // if (deviceMode == 1 || deviceMode == 3 || deviceMode == 31 || deviceMode == 32 || deviceMode == 33 || deviceMode == 15){ // CLOCK MODE:1 and any other mode except Boot, CountUp or Programs.
  if (deviceMode != 2 && deviceMode != 4 ){ // CLOCK MODE:1 and any other mode except Boot, 2:CountUp or 4:Programs.
    ledClockUpper();
    ledSecLowerLeft();
    uint8_t tempByte = timeinfo.tm_mon +1; // January = 0
    if (tempByte > 9){
      ledDigit[3] = tempByte / 10; // First Digit
      ledDigit[2] = tempByte % 10; // Second Digit  
    }
    else {
      ledDigit[3] = tempByte;
      ledDigit[2] = 10;
    }
    tempByte = timeinfo.tm_mday;
    ledDigit[1] = tempByte / 10; // First Digit
    ledDigit[0] = tempByte % 10; // Second Digit
  }
  // fastLEDRefresh();     // apply digit masks and send the 'leds' array out to the actual LED strip NOW, not on next myCycle.
}
void handleSubmitPrefs() {
    String html = HTML_PARTA;
    html += "DojoClock-Submitted";
    html += HTML_PARTB;
    html += "Submision Results</h1>";
    eeprom.begin("prefs", false);
    bool changed = false;
    String tempString;
    // Check and update SSID
    if (server.hasArg("ssid")) {
        tempString = server.arg("ssid");
        if (tempString != prefsData.ssid) {
            eeprom.putString("ssid", tempString);
            prefsData.ssid = tempString;
            html += "<p>SSID updated to: " + tempString + "</p>";
            changed = true;
        }
    }
    // Check and update Password
    if (server.hasArg("password")) {
        tempString = server.arg("password");
        if (tempString != prefsData.password) {
            eeprom.putString("password", tempString);
            prefsData.password = tempString;
            html += "<p>Password updated.</p>"; // For security, don't echo password
            changed = true;
        }
    }

    // bool useBeeper NEW CHECKBOX VERSION
    bool tempBool = server.hasArg("useBeeper");
    if (tempBool != prefsData.useBeeper) {
      eeprom.putBool("useBeeper", tempBool);
      prefsData.useBeeper = tempBool;
      html += "<p>useBeeper updated.</p>";
      changed = true;
    }

    // bool uselightSensor NEW CHECKBOX VERSION
    tempBool = server.hasArg("useLightSensor");
    if (tempBool != prefsData.useLightSensor) {
      eeprom.putBool("useLightSensor", tempBool);
      prefsData.useLightSensor = tempBool;
      html += "<p>useLightSensor updated.</p>";
      changed = true;
    }

// bool useBell
    tempBool = server.hasArg("useBell");
    if (tempBool != prefsData.useBell) {
      eeprom.putBool("useBell", tempBool);
      prefsData.useBell = tempBool;
      html += "<p>useBell updated.</p>";
      changed = true;
    }

// bool nightDisplayOff
    tempBool = server.hasArg("nightDisplayOff");
    if (tempBool != prefsData.nightDisplayOff) {
      eeprom.putBool("nightDisplayOff", tempBool);
      prefsData.nightDisplayOff = tempBool;
      html += "<p>nightDisplayOff updated.</p>";
      changed = true;
    }

// brightnessSlope
    if (server.hasArg("brightnessSlope")) {
         float tempFloat = server.arg("brightnessSlope").toFloat();
        if (tempFloat != prefsData.brightnessSlope && tempFloat >= 0) {
            eeprom.putFloat("brightnessSlope", tempFloat);
            prefsData.brightnessSlope = tempFloat;
            html += "<p>brightnessSlope updated.</p>";
            changed = true;
        }
    }
// luxStabilityVariance
    if (server.hasArg("luxStabilityVariance")) {
         float tempFloat = server.arg("luxStabilityVariance").toFloat();
        if (tempFloat != prefsData.luxStabilityVariance && tempFloat >= 0 && tempFloat <= 100) {
            eeprom.putFloat("luxStabilityVariance", tempFloat);
            prefsData.luxStabilityVariance = tempFloat;
            html += "<p>luxStabilityVariance updated.</p>";
            changed = true;
        }
    }
// brightnessLateral is int8_t to be -128 to 127
    if (server.hasArg("brightnessLateral")) {
      int8_t tempByte = constrain(server.arg("brightnessLateral").toInt(), -128, 127);
      if (tempByte != prefsData.brightnessLateral) {
        eeprom.putChar("brightnessLateral", tempByte);
        prefsData.brightnessLateral = tempByte;
        html += "<p>brightnessLateral updated.</p>";
        changed = true;
      }
    }

// ===== BYTE Preferences stored in NVR ================================
    struct Item {
      const char* key;
      uint8_t& field;
    };
    Item UCharItems[] = {
      { "colorClock",   prefsData.colorClock },
      { "colorSeconds", prefsData.colorSeconds },
      { "colorMonth",   prefsData.colorMonth },
      { "colorDate",    prefsData.colorDate },
      { "taskHour",   prefsData.taskHour },
      { "taskMinute",   prefsData.taskMinute },
      { "luxDarkMode",   prefsData.luxDarkMode },
      { "luxDeltaCovered",   prefsData.luxDeltaCovered },
      { "brightnessMax",   prefsData.brightnessMax },
      { "brightnessMin",   prefsData.brightnessMin },  
      { "brightnessMinDark",   prefsData.brightnessMinDark },  
      { "brightnessStatic",   prefsData.brightnessStatic },
      { "blendSpeed",   prefsData.blendSpeed },
      { "bellStrikeTime",   prefsData.bellStrikeTime },
      { "neglectTime",   prefsData.neglectTime },
      { "lcdBacklightTime", prefsData.lcdBacklightTime },
      { "nightDisplayOffTime", prefsData.nightDisplayOffTime }
    };   
    for (auto &UCharItems : UCharItems) { // for every item in list
      if (server.hasArg(UCharItems.key)) {
        uint8_t tempByte = constrain(server.arg(UCharItems.key).toInt(), 0, 255); // Constrain value from 0-255! Larger will be 255, Negative and text will be 0.
        if (tempByte != UCharItems.field) {
          eeprom.putUChar(UCharItems.key, tempByte);
          UCharItems.field = tempByte;
          html += "<p>"; html += UCharItems.key; html += " updated.</p>";
          changed = true;
        }
      }
    }
    eeprom.end();

    if (!changed) {
        html += "<p>No changes detected for preferences.</p>";
    }

    html += "<h2 class='home-link'><a href='/'>Back to Home</a></h2>";
    html += "</div>";
    html += HTML_FOOTER;
    server.send(200, "text/html", html);
    ledSetup();
}
void wifiClientOff(){
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiClientStatus = 0;
  IP = "0";
}
void wifiClientOn(){
  if (wifiClientStatus == 0) {
    terminal.println(prefsData.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(prefsData.ssid.c_str(), prefsData.password.c_str());
    wifiClientStatus = 0;
    wifiTimeout = wifiTimeoutDefault;
    while (WiFi.status() != WL_CONNECTED && wifiTimeout > 0) {
      wifiTimeout = wifiTimeout -1;
      terminal.print(".");
      delay(1000);
    }
    if (wifiTimeout == 0){
      terminal.println("FAIL");
      wifiClientOff();
    }
    else {
      terminal.println("OK");
      wifiClientStatus = 1;
      wifiSuccess = 1;
      IP = WiFi.localIP();
    }
  }  
}
void wifiSetupAP() {
  // Configure ESP32 as an Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(APssid, APpassword);
  //WiFi.softAP(prefsData.ssid, prefsData.password); // USE for CLIENT!
  IP = WiFi.softAPIP();
  //// Remove the password parameter, if you want the AP (Access Point) to be open
  //WiFi.softAP(APssid, APpassword);
  //IP = WiFi.softAPIP();
  wifiAPStatus = 1;
  }
void wifiServerOn(){
  if (wifiSuccess){wifiClientOn();} else {wifiSetupAP();}
  if (wifiClientStatus || wifiAPStatus){
    // --- Web Server Route Handling ---
    // Handle requests to the root path (Home page)
    server.on("/", handleRoot);

    // Handle requests for the preferences pages and its submission
    server.on("/programs", handlePrograms);
    server.on("/wifi", handleWifi);
    server.on("/prefs", handlePrefs);
    server.on("/submit_prefs", HTTP_POST, handleSubmitPrefs);

    // Set up handlers for each program namespace (0-9)
    for (int i = 0; i < 10; ++i) {
        // Lambda functions are used here to capture the current value of 'i'
        // for each specific program page and its submission handler.
        server.on("/program" + String(i), [i]() {
            handleNamespacePage(i);
        });
        server.on("/submit_program" + String(i), HTTP_POST, [i]() {
            handleSubmitNamespace(i);
        });
    }
    // Start the web server
    server.begin();

    //lcdSetup();
    //terminal.clear();
    //terminal.println("Connect to wifi:");
    if(wifiClientStatus ){
      // lcd.print Stored Client WIFI network.
      //lcd.print(prefsData.ssid);
      //lcdCR();
      terminal.println(prefsData.ssid);
    }
    if(wifiAPStatus){
      //lcd.print(APssid);
      //lcdCR();
      terminal.print("wifi:");
      terminal.println(APssid);
      //lcd.print("Passwd: ");
      //lcd.print(APpassword);
      //lcdCR();
      terminal.print("Pswd:");
      terminal.println(APpassword);
    }
    lcd.setCursor(0,3);
    lcd.print(IP);
    wifiServerStatus = 1;
  }
}
void wifiServerOff(){
  if (wifiClientStatus){
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    wifiClientStatus = 0;
    wifiServerStatus = 0;
  }
  else if(wifiAPStatus){
    WiFi.softAPdisconnect(true); // Disconnect and remove the AP
    WiFi.mode(WIFI_OFF);       // Turn off Wi-Fi radio
    wifiAPStatus = 0;
    wifiServerStatus = 0;
  }
}
void applyTimezone() {
// ===== Your global variables =====
//  bool useDST = 1;
//  char timeZone = 'C'; // E- Eastern, C- Central, M- Mountain, W- Western
//  
//  uint8_t DSTStartSunday = 2;  // Second Sunday in March 2am Begin
//  uint8_t DSTStartMonth  = 3;  // March
//  uint8_t DSTEndSunday   = 1;  // First Sunday in November 2am End
//  uint8_t DSTEndMonth    = 11; // November

// ===== applyTimezone() with no parameters =====
    const char* stdName;
    const char* dstName;
    int baseOffset;  // hours west of UTC (positive number)

    switch (timeZone) {
        case 'E':  // Eastern
            stdName = "EST";
            dstName = "EDT";
            baseOffset = 5;
            break;

        case 'C':  // Central
            stdName = "CST";
            dstName = "CDT";
            baseOffset = 6;
            break;

        case 'M':  // Mountain
            stdName = "MST";
            dstName = "MDT";
            baseOffset = 7;
            break;

        case 'W':  // Western (Pacific)
            stdName = "PST";
            dstName = "PDT";
            baseOffset = 8;
            break;

        default:   // fallback to UTC
            stdName = "UTC";
            dstName = "UTC";
            baseOffset = 0;
            break;
    }

    static char tzString[64];

    if (useDST) {
        // Full DST rule
        sprintf(
            tzString,
            "%s%d%s,M%d.%d.0/2,M%d.%d.0/2",
            stdName,
            baseOffset,
            dstName,
            DSTStartMonth,
            DSTStartSunday,
            DSTEndMonth,
            DSTEndSunday
        );
    } else {
        // No DST — fixed offset only
        sprintf(
            tzString,
            "%s%d",
            stdName,
            baseOffset
        );
    }

    setenv("TZ", tzString, 1);
    tzset();
}
void manualSetTime(int year, int month, int day, int hour, int minute, int second) { // Pass -1 for any field you want to leave unchanged 
    // 1. Apply timezone rules so mktime() interprets local time correctly
    applyTimezone();

    // 2. Get the current LOCAL time
    struct tm localTm;
    if (!getLocalTime(&localTm)) {
        // If time is not set yet, initialize to a safe baseline
        memset(&localTm, 0, sizeof(localTm));
        localTm.tm_year = 2024 - 1900;  // fallback year
        localTm.tm_mon  = 0;            // January
        localTm.tm_mday = 1;            // 1st
    }

    // 3. Replace only the fields the user wants to change
    if (year   >= 0) localTm.tm_year = year - 1900;
    if (month  >= 0) localTm.tm_mon  = month - 1;
    if (day    >= 0) localTm.tm_mday = day;
    if (hour   >= 0) localTm.tm_hour = hour;
    if (minute >= 0) localTm.tm_min  = minute;
    if (second >= 0) localTm.tm_sec  = second;

    // 4. Convert updated LOCAL time → UTC timestamp
    time_t utcTimestamp = mktime(&localTm);

    // 5. Set the ESP32 internal clock
    struct timeval tv = { .tv_sec = utcTimestamp, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
}
void SetupTime(){
    // Sync internal clock with Internet Time Server================
  deviceMode = 0;
  lcdSetup();
  terminal.print("Time Setup:");
  if (wifiClientStatus == 0 && wifiAPStatus !=1 ){
    terminal.println("No Wifi");
    wifiClientOn();
  }
  if (wifiClientStatus == 1){//========================================================================
    terminal.println("Local Time ?");
    wifiTimeout = wifiTimeoutDefault;
    NTPSuccess = 0;
    // --- Step 1: Check for existing time and back it up if found ---
    if (getLocalTime(&timeinfo)){
      terminal.print("Yes, Backing up:");
      backupTimeinfo = timeinfo;
      backupTimeEpoch = mktime(&backupTimeinfo);
      terminal.println("OK");
    } else {
      terminal.println("No, Skip Backup");
    }
    // --- Step 2: Reset the real-time clock to an invalid state ---
    terminal.print("Reset Clock:");
    struct timeval invalidTime;
    invalidTime.tv_sec = 0; // Epoch time 0 is invalid for ESP32's sync check
    invalidTime.tv_usec = 0;
    settimeofday(&invalidTime, nullptr);
    terminal.println("OK");
    
    // --- Step 3: Attempt to sync with the NTP server ---
    wifiTimeout = wifiTimeoutDefault;
    configTime(0, 0, "pool.ntp.org");   // Always sync UTC
    terminal.print("Sync:");
    terminal.println(ntpServer);
    while (!getLocalTime(&timeinfo) && wifiTimeout > 0){ // && is OR test || is AND
      wifiTimeout = wifiTimeout -1 ;
      terminal.print(".");
      delay(1000);
    }
    // --- Step 4: Check for success and restore on failure ---
    if (getLocalTime(&timeinfo)) {
      terminal.println("OK");
      NTPSuccess = 1;
      applyTimezone();
      getLocalTime(&timeinfo);
    } else {
      NTPSuccess = 0;
      terminal.print("FAIL,Use Backup:");
      // Restore the backed-up time if it exists
      if (backupTimeEpoch != 0) {
        struct timeval restoredTime;
        restoredTime.tv_sec = backupTimeEpoch;
        restoredTime.tv_usec = 0;
        settimeofday(&restoredTime, nullptr);
        if(getLocalTime(&timeinfo)){ // Re-fetch the time after restoring
        terminal.println("OK");
        }; 
      } else {
        terminal.println("FAIL");
      }
    }
    wifiClientOff();
  }
  if (wifiClientStatus == 0 || NTPSuccess == 0){ //========================================================================
    // No Wifi so... Manually Set Time
    terminal.print("Local Time?:");
    if(!getLocalTime(&timeinfo)){// On a reboot, local time is still present so check it!
      terminal.println("No");
      terminal.println("Set Default Time");
      manualSetTime(2023, 11, 4, 18, 30, 00); // The moment I received 1st Dan (2023, 11-4 19:30 CDST)!
      }
    else{
      terminal.println("Yes");
    }
  }
  delay(2000); // So you can read screen before LCD resets.
  getLocalTime(&timeinfo); // Last is to updae the timeinfo struct for immeadeate use.
}
void checkSensor(){  
while (!lightMeter.measurementReady(true)) { yield();}
  float luxLast = lux;
  lux = lightMeter.readLightLevel(); // Current Light sample.
  if (lux <= 0){lux = 1;}
  // Calculate DELTA Down Lux % --------------------------
  if( (lux - luxLast) < 0) {luxDelta = (abs(luxLast - lux))/luxLast * 100;}
  if( (lux - luxLast) >= 0) {luxDelta = 0;}

  // Take mulitple samples over time to determine if roomLux is stable and valid -----------------
  if (luxSampleTimer.ended()) {
    luxSamples[luxSampleNum] = lux;
    luxRoomValid = 0;
    if ( ( abs(luxSamples[0] - luxSamples[1]) / luxSamples[0] * 100  ) < prefsData.luxStabilityVariance ){
      if ( ( abs(luxSamples[1] - luxSamples[2]) / luxSamples[1] * 100  ) < prefsData.luxStabilityVariance ){
        luxRoomValid = 1; 
      }
    }
    luxSampleNum = (luxSampleNum +1) % 3; // Increments sample Num 0-2
    luxSampleTimer.set(1000);
  }
  lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE);
}
void hammerBell(byte i){ // 1:Single, 2:Double 3:Triple 4:Double(slow) 5:Triple(slow) 6:Double(fast) 7:Triple(fast) 8:Racecar (1,1...2)
  //(14 ms quietest, 18 "regular") 300 delay is good.
  if (prefsData.useBell){
    lcd.setCursor(14,0); //Char 0 , Row 0
    lcd.write(6); // Bell Symbol
    if (i == 1){ // Single
      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      //delay(250); // So you can see the display
      lcd.setCursor(14,0); //Char 0 , Row 0
      lcd.print(" ");
      return;
    }
    if (i == 8){ //Racecar Bell
      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      delay (800);
      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      delay (800);

      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      delay (125);
      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      delay(750); // So you can see the display
      lcd.setCursor(14,0); //Char 0 , Row 0
      lcd.print(" ");
      return;
    }
    if (i == 2 || i == 3 ){ bellDelay = 300;} // Set the delay to Regular 300ms
    if (i == 4 || i == 5 ){ bellDelay = 600;} // Set the delay to Slow 600ms
    if (i == 6 || i == 7 ){ bellDelay = 125;} // Set the delay to Fast 125ms

    if (i == 2 || i == 4 || i == 6 ){ // Double
      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      delay (bellDelay);
      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      //delay(250); // So you can see the display
      lcd.setCursor(14,0); //Char 0 , Row 0
      lcd.print(" ");
      return;
    }
    if (i == 3 || i == 5 || i == 7 ){ // Triple
      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      delay (bellDelay);
      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      delay (bellDelay);
      digitalWrite(BellPin, HIGH);
      delay (prefsData.bellStrikeTime);
      digitalWrite(BellPin, LOW);
      //delay(750); // So you can see the display
      lcd.setCursor(14,0); //Char 0 , Row 0
      lcd.print(" ");
      return;
    }
  }  
}   
void programPause(uint8_t ringBell){
  if (prefsData.useBeeper){ digitalWrite(BeeperPin, HIGH); }
  if (programPaused){
    timerD1.ResumeTimer();
    timerU2.ResumeTimer();
    timerD3.ResumeTimer();
    if (prefsData.useBell && ringBell == 1) hammerBell(1);
    lcd.setCursor(14,1); //Char 0 , Row 0
    lcd.print(" "); // Clear Pause Symbol
    // Restore LED colors
    color100 = color100Last; // digits 0,1
    color102 = color102Last; // digits 2,3
    colorLC = colorLCLast;   // lower colon
    color104 = color104Last; // digits 4,5
    color106 = color106Last; // digits 6,7,8,9 & UC
    onboardled[0] = onboardled[1]; // [1] is for last state
    paintEntire = 0;
    programPaused = 0;
  }
  else {
    timerD1.PauseTimer();
    timerU2.PauseTimer();
    timerD3.PauseTimer();
    lcd.setCursor(14,1); //Char 0 , Row 0
    lcd.write(4); // Pause Symbol
    if (prefsData.useBell && ringBell == 1) hammerBell(1);
    // Store current LED colors
    color100Last = color100; // digits 0,1
    color102Last = color102; // digits 2,3
    colorLCLast = colorLC;   // lower colon
    color104Last = color104; // digits 4,5
    color106Last = color106; // digits 6,7,8,9 & UC
    onboardled[1] = onboardled[0]; // [1] is for last state
    color255 = 10;
    paintEntire = 1;
    onboardled[0] = CRGB::Purple;
    programPaused = 1;
  }
  digitalWrite(BeeperPin, LOW);
}
unsigned long getTimeMS(CountUpDownTimer &t) {
    return  (unsigned long)t.ShowHours()   * 3600000UL +
            (unsigned long)t.ShowMinutes() * 60000UL   +
            (unsigned long)t.ShowSeconds() * 1000UL    +
            (unsigned long)t.ShowMilliSeconds();
}
void setTimerMinSec(CountUpDownTimer &t, uint8_t minutes, uint8_t seconds) {
    if (minutes > 99) minutes = 99;
    if (seconds > 59) seconds = 59;

    unsigned long tempSeconds =
        t.ShowTotalSeconds() +
        (unsigned long)minutes * 60UL +
        (unsigned long)seconds;

    if (tempSeconds > 5999UL) tempSeconds = 5999UL; // 99:59

    t.SetTimer(tempSeconds);
}
void action(){                // CMD A MACRO  Set Action
  sourceT = 1; // TimerD1 This works!
  //sourceV = 0; // Use CMD U in the program instead
  //sourceW = 2; // Rounds, Use CMD W in the program instead
  if ( useBonus ){
    uint8_t tempByte = bonusRegister[0] *10 + bonusRegister[1]; // Number from 0-99 % ???
    if (tempByte >= 0 ){
      float tempInt;
      if (tempByte > 99) {tempByte = 99;}
      if (!blindBonus){ // No Blind Bonus
        tempInt = ((fightRegister[0] * 10 + fightRegister[1]) * 60) + (fightRegister[2] * 10 + fightRegister[3]);
        tempInt = tempInt + (int)(tempInt * (random(0, tempByte) / 100.0f) ); // Float to keep decimal for Random # values for calc, then int to strip it at end.
        timerD1.SetTimer(tempInt);
        blindStep = 0; // Extra blind Step to do? 1 = yes
      }
      if ( blindBonus ){ // Blind Bonus
        tempInt = ((fightRegister[0] * 10 + fightRegister[1]) * 60) + (fightRegister[2] * 10 + fightRegister[3]);
        timerD1.SetTimer( tempInt );
        blindBonusTime =  (int)(tempInt * (random(0, tempByte) / 100.0f) ); // Global variable to pass this to CMD a later.
        // timerU2.SetTimer( (int)(tempInt * (random(0, tempByte) / 100.0f) ) ); // This is the extra random bonus time.
        blindStep = 1; // Extra blind Step to do? 1 = yes
      }
    }  
    if (tempByte == 0) { // Bonus is set to 0 !
      timerD1.SetTimer(  ((fightRegister[0] * 10 + fightRegister[1]) * 60) + (fightRegister[2] * 10 + fightRegister[3])   );
      blindStep = 0;
    }
  }
  else { // No Bonus time used
    timerD1.SetTimer(  ((fightRegister[0] * 10 + fightRegister[1]) * 60) + (fightRegister[2] * 10 + fightRegister[3])   );
    blindStep = 0;
  }
  //sourceU = 1; // TimerD1 on top LED display USE CMD U inline instead.
  lcdSetup(); // repaint the LCD's unchanging items with those recently changed in prior commands.
  lcdRefresh();
  lcd.setCursor(13,1); //Char 0 , Row 0
  lcd.write(3); // Play
  lcd.setCursor(15,1); //Char 0 , Row 0
  lcd.print("FIGHT");
  readyForAction = 1;
  cmdStarted = 1;
  cmdCompleted = 1;
}  
void actionGo(){              // CMD a Start what was set in the Action CMD
  if (!cmdStarted){
    if (!readyForAction){ cmdCompleted = 1; return; } // If Action wasn't set, skip this command!
    timerD1.StartTimer();
    onboardled[0] = CRGB::Green;
    cmdStarted = 1;
  } //-----------------------------------------------
  if (!cmdCompleted){
    if (blindStep){ // Executes in order 1, then 0
      //timerD1.Timer();
      if ( timerD1.TimeCheck() ){
        timerU2.ResetTimer();
        timerD1.ResetTimer();
        sourceT = 2;
        sourceU = 2;
        timerD1.SetTimer(blindBonusTime);
        //timerD1.SetTimer(timerU2.ShowTotalSeconds());
        timerD1.StartTimer();
        timerU2.StartTimer();
        ledRefresh();
        lcdRefresh();
        blindStep = 0;
      }
    }
    if (!blindStep ){
      //timerD1.Timer();
      //if (blindBonus){timerU2.Timer();}
      if ( timerD1.TimeCheck() ){
        if (blindBonus){timerU2.StopTimer();}
        //timerD1.ResetTimer();
        ledRefresh();
        lcdRefresh();
        readyForAction = 0;
        cmdCompleted = 1;
      }
    }
  } 
}
void bonus(){                 // CMD B  Bonus %
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
    if (tempByte > 0) {
      bonusRegister[0] = tempByte / 10;
      bonusRegister[1] = tempByte % 10;
      useBonus = 1;
    }
    useBonus = 1;
    cmdCompleted = 1;
}
void blind(){                 // CMD b  blind
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte == 1){blindBonus = 1;}
  if (tempByte == 0){blindBonus = 0;}
  cmdCompleted = 1;
}
void commitToTimer(){         // CMD C Commit to Timer
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  cmdStarted = 1;
  if (tempByte == 1){
    if ( timerD1.TimeCheck() ){
      cmdCompleted = 1;    
    }
  }  
  if (tempByte == 3){
    if ( timerD3.TimeCheck() ){
      cmdCompleted = 1;    
    }
  }  
}
void downTimeMin(){           // CMD D Set Rest Registry MINUTES
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte >= 0 && tempByte < 100) {
    restRegister[0] = tempByte / 10;
    restRegister[1] = tempByte % 10;
  }
  useRest = 1;
  cmdCompleted = 1;
}
void downTimeSec(){           // CMD d Set Rest Registry SECONDS
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte >= 0 && tempByte < 60 ) {
    restRegister[2] = tempByte / 10;
    restRegister[3] = tempByte % 10;
  }
  useRest = 1;
  cmdCompleted = 1;
}  
void effect(){                // CMD E  LED special effects
  if (!cmdStarted){
    uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
    if (tempByte > 93 ){          // Limit-  108(color255) - 15 (solid colors) = 93 effect choices
      cmdCompleted = 1;
      return;
    }
    if (fastEffectTime <= 0) fastEffectTime = 5;    // 5 SECONDS MINIMUM even if time not set.
    specialEffectsTimer.set(fastEffectTime*1000);   // set run timer with value from CMD e set earlier.
    
    //  if (color255 == 16){ confettiPops(); }        // #1
    //  if (color255 == 17){ confettiClusters(); }    // #2
    //  if (color255 == 18){ confettiDrift(); }       // #3
    //  if (color255 == 19){ confettiParty(); }       // #4
    //  if (color255 == 20){ sinelonWide(); }         // #5 cool
    //  if (color255 == 21){ juggle(); }              // #6
    //  if (color255 == 22){ bpm(); }                 // #7 pulsing w/ rainbow colors
    //                //23                            // #8 EiffelSparkle Random Color using 27-35 [color255 values]
    //                //24                            // #9 EiffelSparkle MAX Random Color using 36-44
    //                //25                            // #10 SloshColors Random using 45-80
    //                //26                            // #11 bpm 2 random colors 81-112

    if ( tempByte == 0 ) {                            // #0 Random base effects C255# 16-26 (weighted) different from previous one.
      uint8_t newValue;                               // Weighting: 1,2,3,4,5,6,7, 8,8,9,10,10,11 (13 options) Randomizer: 16-28> 16-22, 23, 23, 24, 25, 25, 26
      do { newValue = random( 16, 29 ); } while ( newValue == color255 );
      if ( newValue <= 22) color255 = newValue;
      if ( newValue == 23) color255 = random( 27, 36 );   // Effect #8, 23
      if ( newValue == 24) color255 = random( 27, 36 );   // Effect #8, 23
      if ( newValue == 25) color255 = random( 36, 45 );   // Effect #9, 24
      if ( newValue == 26) color255 = random( 45, 81 );   // Effect #10,25
      if ( newValue == 27) color255 = random( 45, 81 );   // Effect #10,25
      if ( newValue == 28) color255 = random( 81, 109 );  // Effect #11,26
    }  
    if (tempByte >= 1 && tempByte <= 7 ) { color255 = tempByte + 15; } // why +15? 1-15 are base colors and random in color255 space!
    if (tempByte == 8 ){                            // #8 (23) EiffelSparkle Random Colors using 27-35 [color255 values]
      color255 = random( 27, 36 );
    }
    if (tempByte == 9 ) {                           // #9 (24) EiffelSparkle MAX Random Color using 36-44
      color255 = random( 36, 45 );
    }
    if (tempByte == 10 ){                           // #10 (25) SloshColors Random using 45-80
      color255 = random( 45, 81 );
    }
    if (tempByte == 11 ){                           // #11 (26) bpm 2 random colors 81-108
      color255 = random( 81, 109 );
    }
    if (tempByte >= 12 ){                           // Individual specific effects. Effect #12 (27) and up
      color255 = tempByte + 15;
    }
    paintEntire = 1;
    useLedDigitMask = 0; // Clear number mask usage for full display
    cmdStarted = 1;
  } // -----------------------------------------------------------
  if (!cmdCompleted){ // Run correct effect until timer expires.
    if (specialEffectsTimer.ended()){
      useLedDigitMask = 1;
      paintEntire = 0; // Switches display back to pieces and their former colors.
      cmdCompleted = 1;
      return;
    }
  }
}
void effectTime(){            // CMD  Set special effect time (0[5]-255 seconds)
  fastEffectTime = namespaces[progNum].byteElements[progCmd];
  if ( fastEffectTime <=4 ) fastEffectTime = 5;
  cmdCompleted = 1;
}
void fightMin(){              // CMD F  fight Reg MIN
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte > 0) {
    fightRegister[0] = tempByte / 10;
    fightRegister[1] = tempByte % 10;
  }
  useFight = 1;
  cmdCompleted = 1;
}
void fightSec(){              // CMD f  fight Reg SEC
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte > 0) {
    fightRegister[2] = tempByte / 10;
    fightRegister[3] = tempByte % 10;
  }
  useFight = 1;
  cmdCompleted = 1;
}
void goTimers(){              // CMD G  Go Timers 1,2,3,12,13,23,123
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte == 1){timerD1.StartTimer();}
  if (tempByte == 2){timerU2.StartTimer();}
  if (tempByte == 3){timerD3.StartTimer();}
  if (tempByte == 12){timerD1.StartTimer();timerU2.StartTimer();}
  if (tempByte == 13){timerD1.StartTimer();timerD3.StartTimer();}
  if (tempByte == 23){timerU2.StartTimer();timerD3.StartTimer();}
  if (tempByte == 123){timerD1.StartTimer();timerU2.StartTimer();timerD3.StartTimer();}
  cmdCompleted = 1;
}
void hammerBell(){            // CMD H  Hammer Bell
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte > 0 && tempByte < 9){
    hammerBell(tempByte);
  }
  cmdCompleted = 1;
}
void setRoundsInterval(){     // CMD I  Rounds Interval Set
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte > 99){tempByte = 99;} // can be 0-255, hence clamping!
  roundsInterval = tempByte;
  cmdCompleted = 1;
}
void setRoundsInc(){          // CMD i Increment or Decrement?? bool roundsInc = 1; // 0 = decrement.
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if ( tempByte >= 0 && tempByte < 2 ) { // This is a BOOL, so if not 0 or 1, do nothing.
  roundsInc = tempByte;
  }
  cmdCompleted = 1;
}
void loopHead(){              // CMD L  Loop Head
  uint8_t tempByte;
  if (loopSet){ // If this is set already.. It's a repeat visit in the loop!
    //  if (useRounds){ // Update Rounds Registry
    //    tempByte = (roundsRegister[0] * 10) + roundsRegister[1];
    //    if (roundsInc){ ++tempByte;} else {--tempByte;}
    //    roundsRegister[0] = tempByte / 10;
    //    roundsRegister[1] = tempByte % 10;
    //  }
    cmdCompleted = 1;
    --loopCount; 
    return;   // repeat loop visit. Exit now so you don't run the set up again!
  }
  if (!cmdStarted) { // ============= Runs first time through the Loop Head only.==========================
    if (rounds2Loops){ // Manual buttons used! Rounds = Loop Count! -------------------
      tempByte = (roundsRegister[0] * 10) + roundsRegister[1]; // No error checking as 99 is max storage!
      roundsInterval = 1;
      if (tempByte > 0 && tempByte < 100){ // if data is 1-99 here, we're counting down.
        roundsInc = 0;
        loopCount = tempByte - 1;
      }
      if (tempByte == 0){ // if cmd data is zero, were counting up to 99.
        roundsInc = 1;
        loopCount = 98;
        roundsRegister[0] = 0;
        roundsRegister[1] = 1;
      }
    }
    else { // NOT USING ROUNDS AS LOOP COUNT! -----------------
      tempByte = namespaces[progNum].byteElements[progCmd]; // USE CMD DATA VALUE NOT REGISTRY!
      if (tempByte > 99){tempByte = 99;} // can be 0-255, hence clamping!
      loopCount = tempByte - 1;
      // Default roundsInc is 0 .... will count down
      // Default roundsInterval is 1 .... by 1
      
      //roundsRegister[0] = tempByte / 10;
      //roundsRegister[1] = tempByte % 10;
    }

    //  if (tempByte > 0 && tempByte < 100){ // if data is 1-99 here, we're counting down.
    //      roundsInc = 0;
    //      loopCount = tempByte - 1;
    //      if (!useRounds) {
    //        roundsRegister[0] = tempByte / 10;
    //        roundsRegister[1] = tempByte % 10;
    //      }
    //    }
    //    else {
    //    if (tempByte == 0){ // if cmd data is zero, were counting up to 99.
    //      roundsInc = 1;
    //      loopCount = 98;
    //      roundsRegister[0] = 0;
    //      roundsRegister[1] = 1;
    //    }
    //  }

    loopSet = 1;
    cmdStarted = 1;
    cmdCompleted = 1;
  }  
}
void loopTail(){              // CMD l  Loop Tail
  if (!loopSet){ // Loop Head L never set Loop or it doesn't exist. Get out!
    cmdCompleted = 1;
    return;
  }  
  if (loopCount > 0){ // if Loop Count > 0, more loops to go, FIND Head!
    for (--progCmd; progCmd >= 0; --progCmd){
      if (namespaces[progNum].charElements[progCmd] == 'L'){
        cmdStarted = 0;
        cmdCompleted = 0; // This is changing progCmd directly, so it needs to be 0 so the new cmd if found here can be executed!
        return;
      }
    }
  }   
  if (loopCount == 0){cmdCompleted = 1;} // Loop Count == 0, Loop Concluded! 
}
void meditate(){              // CMD M MACRO  Meditate / Rest
  uint8_t tempByte;
  if (!cmdStarted) {
    
    if (useRest){
      if (!roundsInc && (roundsRegister[0] * 10 + roundsRegister[1]) == 1){ cmdCompleted = 1; return;} // Don't need the final rest in the last round counting down! 
      tempByte = ((restRegister[0] * 10 + restRegister[1]) * 60) + (restRegister[2] * 10 + restRegister[3]); 
    }

    if (!useRest) { 
      tempByte = namespaces[progNum].byteElements[progCmd];
    }

    if (tempByte == 0){ cmdCompleted = 1; return; }// If Rest Reg is empty or M 0 is called. No Rest.. Do nothing and get out.

    // We've GOT Rest! Do it!
    // change LED Color and LCD screen "REST"
    lcdSetup(); // to show any changes in last few commands.
    lcd.setCursor(13,1); //Char 0 , Row 0
    lcd.write(5); // Heart
    lcd.setCursor(15,1); //Char 0 , Row 0
    lcd.print("REST ");
    onboardled[0] = CRGB::Red;
    sourceT = 1;
    timerD1.ResetTimer();
    timerD1.SetTimer(tempByte);
    lcdRefresh();
    ledRefresh();
    delay(1000);
    timerD1.StartTimer();
    lcdRefresh();
    ledRefresh();
    cmdStarted = 1;
  } //---------------------------------------------------
  if (!cmdCompleted){
    if ( timerD1.TimeCheck() ){ // Resets on expiration.
      // timerD1.ResetTimer();
      lcdRefresh();
      ledRefresh();
      cmdCompleted = 1;
    }
  } 
}
void napTime(){               // CMD N Nap Time.. Do nothing for a while...
  if (cmdStarted == 0){
    napTimer.reset();
    napTimer.set(namespaces[progNum].byteElements[progCmd] * 1000);
    cmdStarted = 1;
  } 
  if (!cmdCompleted){
    if (napTimer.ended()){
      cmdCompleted = 1;
    }
  }
}
void userPause(){             // CMD P  Pause, Wait for user interaction!
  if (!cmdStarted){
    uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
    if (tempByte > 0) tempByte = 1;
    programPause(tempByte);
    cmdStarted = 1;
  }
  if (programPaused == 0){
    cmdCompleted = 1;
  }
}
void resetProgramMode(){      // CMD Q  Exit and reset program
  timerD1.ResetTimer();
  timerU2.ResetTimer();
  timerD3.ResetTimer();
  //tempTime = 0;
  napTimer.reset();
  cmdStarted = 0;
  cmdCompleted = 0;
  progNum = 0;
  progCmd = 0;
  loopCount = 0;
  loopSet = 0;
  useFight = 0;
  for (int i = 0; i < 4; ++i) { fightRegister[i] = 0; }
  useRest = 0;
  for (int i = 0; i < 4; ++i) { restRegister[i] = 0; }
  useBonus = 0;
  blindBonus = 0;
  blindStep = 0;
  blindBonusTime = 0;
  for (int i = 0; i < 2; ++i) { bonusRegister[i] = 0; }
  useRounds = 0;
  roundsInc = 1;
  roundsInterval = 1;
  rounds2Loops = 0;
  roundsRegister[0] = 0;
  roundsRegister[1] = 1;
  // for (int i = 0; i < 2; ++i) { roundsRegister[i] = 0; }
  useLedDigitMask = 1;
  programPaused = 0;
  //runningFastEffect = 0;
  specialEffectsTimer.reset();
  deviceMode = 1;
}
void setRounds(){             // CMD R Set Rounds Registry
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte > 99){tempByte = 99;} // can be 0-255, hence clamping!
  roundsRegister[0] = tempByte / 10;
  roundsRegister[1] = tempByte % 10;
  useRounds = 1;
  cmdCompleted = 1;
  //        
  //        
  //        uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  //        if (tempByte > 0 && tempByte < 100 ) {
  //          roundsRegister[0] = tempByte / 10;
  //          roundsRegister[1] = tempByte % 10;
  //        }
  //        
  //        if (tempByte >= 100 && tempByte <= 255 ) {
  //          uint8_t hundredsPlace = tempByte / 100; // hundreds place = 1:Subtract, 2:Add
  //          uint8_t roundsNewOperator = tempByte % 100; //Decimal Remainder of div 100 = tens and ones. (0-99)
  //          uint8_t roundsCurrentValue = (roundsRegister[0] * 10) + roundsRegister[1];
  //          if (hundredsPlace == 1){ // Subtract New Operator
  //            int8_t roundsSubResults = roundsCurrentValue - roundsNewOperator; // int8_t (-127 to 127), max values would be -99 to 99)
  //            if ( roundsSubResults < 0 ){ 
  //              roundsRegister[0] = 0;
  //              roundsRegister[1] = 0;
  //            }
  //            else {
  //              roundsRegister[0] = roundsSubResults / 10;
  //              roundsRegister[1] = roundsSubResults % 10;
  //            }
  //          }  
  //          if (hundredsPlace == 2){ // Add New Operator
  //            uint8_t roundsAddResults = roundsCurrentValue + roundsNewOperator; // uint8_t (0 to 255), max values would be 0 to 198)
  //            if ( roundsAddResults >= 99 ){ 
  //              roundsRegister[0] = 9;
  //              roundsRegister[1] = 9;
  //            }
  //            else {
  //              roundsRegister[0] = roundsAddResults / 10;
  //              roundsRegister[1] = roundsAddResults % 10;
  //            }
  //          }
  //        }  
}
void RoundsExe(){             // CMD r Execute Rounds Calcs
  if (useRounds){ // Special Case of Manual Entry setting things.
    uint8_t tempByte;
    if (roundsInc){ // Then Loop head says "Count Up!"
      tempByte = (roundsRegister[0]* 10) + roundsRegister[1];
      tempByte = tempByte + roundsInterval;
      if (tempByte >= 99){resetProgramMode();}
    }  
    if (!roundsInc){ // Counting DOWN.... Use loopCount to set Rounds Reg
      tempByte = (uint8_t)constrain(loopCount, 0, 99);
    }  
    roundsRegister[0] = tempByte / 10;
    roundsRegister[1] = tempByte % 10;       
    cmdCompleted = 1;
    return;
  }  
  else { // Use Interval, inc/dec etc...
    uint8_t tempByte;
    int tempInt = (roundsRegister[0]* 10) + roundsRegister[1];
    if (roundsInc){
      tempInt = tempInt + roundsInterval;
      if (tempInt > 99){tempInt = 99;}
    }
    if (!roundsInc){
      tempInt = tempInt - roundsInterval;
      tempByte = (uint8_t)constrain(tempInt, 0, 255); // prevents negatives
    }
    roundsRegister[0] = tempByte / 10;
    roundsRegister[1] = tempByte % 10;
    cmdCompleted = 1;
  }
}
void stopTimers(){            // CMD "S" Stop Timers
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte == 1){timerD1.StopTimer();}
  if (tempByte == 2){timerU2.StopTimer();}
  if (tempByte == 3){timerD3.StopTimer();}
  if (tempByte == 12){timerD1.StopTimer();timerU2.StopTimer();}
  if (tempByte == 13){timerD1.StopTimer();timerD3.StopTimer();}
  if (tempByte == 23){timerU2.StopTimer();timerD3.StopTimer();}
  if (tempByte == 123){timerD1.StopTimer();timerU2.StopTimer();timerD3.StopTimer();}
  cmdCompleted = 1;
}
void setDisplayT(uint8_t i){  // CMD "T"   Set source of LCD's main clock ( 0:Clock, 1:timerD1, 2: timerU2, 3:timerD3)
  if (i <= 3 ) { sourceT = i;}
  cmdCompleted = 1; 
}
void setDisplayU(uint8_t i){  // CMD "U"   Set source and looks of LED top section. ( 0:Clock, 1:timerD1, 2: timerU2 )
  sourceU = i / 100; // hundreds place = 0:Clock, 1:timerD1, 2: timerU2 
  uint8_t tempColor = i % 100;
  if ( tempColor <= maxColorNum ) {
    color106 = tempColor;
    cmdCompleted = 1;
    return;
  } 
  if ( tempColor == (maxColorNum + 1) ){ // One above top color number is "choose a random color"
    uint8_t newColor;
    do { newColor = random(1, (maxColorNum + 1)); } // random between 1 & 14
    while (newColor == color106); // Guarantee a new color.
    color106 = newColor;
    cmdCompleted = 1;
    return;
  }
  if ( tempColor > maxColorNum ) {
   color106 = maxColorNum;
   cmdCompleted = 1;
   return;
  } 
}
void setDisplayV(uint8_t i){  // CMD "V"   Set the source and looks of LED bottom right section. ( 0:Clock, 1:timerD1, 2: timerU2 )
  sourceV = i / 100;
  uint8_t tempColor = i % 100;
  if ( tempColor <= maxColorNum ) {
    color103 = tempColor;
    cmdCompleted = 1;
    return;
  } 
  if ( tempColor == (maxColorNum + 1) ){ // One above top color number is "choose a random color"
    uint8_t newColor;
    do { newColor = random(1, (maxColorNum + 1)); } // random between 1 & 14
    while (newColor == color106); // Guarantee a new color.
    color103 = newColor;
    cmdCompleted = 1;
    return;
  }
  if ( tempColor > maxColorNum ) {
   color103 = maxColorNum;
   cmdCompleted = 1;
   return;
  } 
}
void setDisplayW(uint8_t i){  // CMD "W"   Set the source and looks of LED bottom left section. ( 0:Clock SEC, 1:timerU2 Hours, 2: Rounds Registry )
  sourceW = i / 100;
  uint8_t tempColor = i % 100;
  if ( tempColor <= maxColorNum ) {
    color104 = tempColor;
    cmdCompleted = 1;
    return;
  } 
  if ( tempColor == (maxColorNum + 1) ){ // One above top color number is "choose a random color"
    uint8_t newColor;
    do { newColor = random(1, (maxColorNum + 1)); } // random between 1 & 14
    while (newColor == color106); // Guarantee a new color.
    color104 = newColor;
    cmdCompleted = 1;
    return;
  }
  if ( tempColor > maxColorNum ) {
   color104 = maxColorNum;
   cmdCompleted = 1;
   return;
  } 
}
void setTimerD1Min(){         // CMD "X" Set / add timerD1 MINUTES
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte == 255) {timerD1.ResetTimer();}
  else setTimerMinSec(timerD1, tempByte, 0);
  cmdCompleted = 1;
}
void setTimerD1Sec(){         // CMD "x" Set / add timerD1 SECONDS
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte == 255) {timerD1.ResetTimer();}
  else setTimerMinSec(timerD1, 0, tempByte);
  cmdCompleted = 1;
}
void setTimerU2Min(){         // CMD "Y" Set / add timerU2 MINUTES
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte == 255) {timerU2.ResetTimer();}
  else setTimerMinSec(timerU2, tempByte, 0);
  cmdCompleted = 1;
}
void setTimerU2Sec(){         // CMD "y" Set / add timerU2 SECONDS
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte == 255) {timerU2.ResetTimer();}
  else setTimerMinSec(timerU2, 0, tempByte);
  cmdCompleted = 1;
}
void setTimerD3Min(){         // CMD "Z" Set / add timerD3 MINUTES
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte == 255) {timerD3.ResetTimer();}
  else setTimerMinSec(timerD3, tempByte, 0);
  cmdCompleted = 1;
}
void setTimerD3Sec(){         // CMD "z" Set / add timerD3 SECONDS
  uint8_t tempByte = namespaces[progNum].byteElements[progCmd];
  if (tempByte == 255) { timerD3.ResetTimer();}
  //else timerD3.SetTimer(tempByte); // Seconds version.
  else setTimerMinSec(timerD3, 0, tempByte);
  cmdCompleted = 1;
}
uint8_t convertKey(char keyPressed)
{
  switch (keyPressed)
  {
  case 'G': //Enter
    return 15;
    break;
  case 'S': //Cancel
    return 14;
    break;
  case 'A': //Up
    return 13;
    break;
  case 'B': //Right
    return 12;
    break;
  case 'C': //Left
    return 11;
    break;
  case 'D': //Down
    return 10;
    break;
  case '0':
    return 0;
    break;
  case '1':
    return 1;
    break;
  case '2':
    return 2;
    break;
  case '3':
    return 3;
    break;
  case '4':
    return 4;
    break;
  case '5':
    return 5;
    break;
  case '6':
    return 6;
    break;
  case '7':
    return 7;
    break;
  case '8':
    return 8;
    break;
  case '9':
    return 9;
    break;
  }
  return -1;
}
void buttonStart(){                   // BUTTON (START)
  //START=============//START================//START=================//START==================//START================
  // Panel LED lower, change color to default??
  //  if (deviceMode == 5, 6, 7) do nothing
  if (deviceMode == 5 || deviceMode == 6 || deviceMode == 7 || deviceMode == 11 || deviceMode == 12 || deviceMode == 14 || deviceMode == 15 ){return;}
  if ( deviceMode == 1 || deviceMode == 255){  //Start was pressed in clock mode. Begin Count Up operations.
    timerU2.ResetTimer();
    timerU2.StartTimer();
    deviceMode = 2;
    return;
    }
  if (deviceMode == 2 || deviceMode == 4){   
    // Already in Countup mode.. Pause Toggle? (Any Key except STOP)
    programPause(1); //Ring Bell
    return;
    }
  if (deviceMode == 3 || deviceMode == 31 || deviceMode == 32 || deviceMode == 33){ 
    // Manual Entry Complete! Run registries as they are into a program.
    progNum = 10;
    progCmd = 0;
    deviceMode = 4;
    sourceT = 1; // LCD to show TimerD1 before lcdRefresh
    lcdRefresh();
    return;
    }  
  if (deviceMode == 111){ // New Month Entry
    float tempInt = (timeRegister[0]*10 + timeRegister[1]);
    if (tempInt > 0 && tempInt < 13){
      //getLocalTime(&timeinfo);
      //SetClock(timeinfo.tm_year + 1900, tempInt, timeinfo.tm_mday,timeinfo.tm_hour,timeinfo.tm_min, timeinfo.tm_sec);
      manualSetTime(-1, tempInt, -1, -1, -1, -1);
      getLocalTime(&timeinfo);
      NTPSuccess = 0;
    }
    deviceMode = 11;
    lcdSetup();
    return;
  }
  if (deviceMode == 112){ // New Day Entry
    float tempInt = (timeRegister[0]*10 + timeRegister[1]);
    if (tempInt > 0 && tempInt < 32){
      //getLocalTime(&timeinfo);
      //SetClock(timeinfo.tm_year + 1900 , timeinfo.tm_mon + 1 , tempInt,timeinfo.tm_hour,timeinfo.tm_min, timeinfo.tm_sec);
      manualSetTime(-1,-1, tempInt, -1, -1, -1);
      getLocalTime(&timeinfo);
      NTPSuccess = 0;
    }
    deviceMode = 11;
    lcdSetup();
    return;
  }
  if (deviceMode == 113){ // New Year Entry
    float tempInt = (timeRegister[0]*1000 + timeRegister[1]*100 + timeRegister[2]*10 + timeRegister[3]);
    if (tempInt > 2022 && tempInt < 3000){
      //getLocalTime(&timeinfo);
      //SetClock(tempInt, timeinfo.tm_mon +1 , timeinfo.tm_mday,timeinfo.tm_hour,timeinfo.tm_min, timeinfo.tm_sec);
      manualSetTime(tempInt, -1, -1, -1, -1, -1);
      getLocalTime(&timeinfo);
      NTPSuccess = 0;
    }
    deviceMode = 11;
    lcdSetup();
    return;
  }
  if (deviceMode == 121 ) {//New Time Entry
    // Set clock with TimeRegister values.
    //timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour);
    //if (timeRegister[0] == 0){hrTemp = timeRegister[1];} else{ hrTemp = }
    //(timeRegister[2]*10 + timeRegister[3])
    if( (timeRegister[0]*10 + timeRegister[1]) < 24 && (timeRegister[2]*10 + timeRegister[3]) < 60 ){
      //getLocalTime(&timeinfo);
      //SetClock(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, (timeRegister[0]*10 + timeRegister[1]), (timeRegister[2]*10 + timeRegister[3]), 00);
      manualSetTime(-1, -1, -1, (timeRegister[0]*10 + timeRegister[1]), (timeRegister[2]*10 + timeRegister[3]), 0);
      getLocalTime(&timeinfo);
      NTPSuccess = 0;
      // This WORKS (except for 5 hours off) !! SetClock(2025, 6, 7, (timeRegister[0]*10 + timeRegister[1]), (timeRegister[2]*10 + timeRegister[3]), 00);
      //configTime((TZOffset() + (daylightOffset_sec * DST)), 0 , ntpServer);
    }  
    deviceMode = 12;
    return;
  }
  if (deviceMode == 131){ // DST Dates: START Sunday SAVE Entry
    if (timeRegister[0] > 0 && timeRegister[0] < 6){
      DSTStartSunday = timeRegister[0];
      eeprom.begin("prefs", false); // 'false' for read-write mode
      eeprom.putChar("DSTStartSunday", DSTStartSunday);
      eeprom.end();
      applyTimezone();
    }  
    deviceMode = 13;
    lcdSetup();
    return;
  }
  if (deviceMode == 132){ // DST Dates: START Month SAVE Entry
    float tempInt = timeRegister[0] *10 + timeRegister[1];
    if (tempInt > 0 && tempInt < 13){
      DSTStartMonth = tempInt;
      eeprom.begin("prefs", false); // 'false' for read-write mode
      eeprom.putInt("DSTStartMonth", DSTStartMonth);
      eeprom.end();
      applyTimezone();
    }
    deviceMode = 13;
    lcdSetup();
    return; 
  }
  if (deviceMode == 133){ // DST Dates: END Sunday SAVE Entry
    if (timeRegister[0] > 0 && timeRegister[0] < 6){
      DSTEndSunday = timeRegister[0];
      eeprom.begin("prefs", false); // 'false' for read-write mode
      eeprom.putChar("DSTEndSunday", DSTEndSunday);
      eeprom.end();
      applyTimezone();
    }
    deviceMode = 13;
    lcdSetup();
    return;
  }
  if (deviceMode == 134){ // DST Dates: END Month SAVE Entry
    float tempInt = timeRegister[0] *10 + timeRegister[1];
    if (tempInt > 0 && tempInt < 13){
      DSTEndMonth = tempInt;
      eeprom.begin("prefs", false); // 'false' for read-write mode
      eeprom.putInt("DSTEndMonth", DSTEndMonth);
      eeprom.end();
      applyTimezone();
    }
    deviceMode = 13;
    lcdSetup();
    return;  
  }
}
void buttonStop(){                    // BUTTON (STOP)
  //STOP====================//STOP======================//STOP============================//STOP=======================//STOP===================
  //  if (deviceMode == 1 ) do nothing
  if (deviceMode == 1){return;}
  if (deviceMode == 255){deviceMode = 1;}
  if (deviceMode == 2 || deviceMode == 4){ // Stop and reset all timers, reset all registries, flags, program counters & change to clock
    resetProgramMode();
    return;
  }
  if (deviceMode == 3 || deviceMode == 31 || deviceMode == 32 || deviceMode == 33){ // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
      deviceMode = 1;
    return; 
  }
  if (deviceMode == 5 || deviceMode == 15){ // 15 is continous sensor display
    deviceMode = 1; // Change to clock
    return;
  }
  if (deviceMode == 6 ){ 
    deviceMode = 5; // Change to Mode 5
    return;
  }
  if (deviceMode == 7 ){  // Change to mode 5
    return;
  }
  if (deviceMode == 11 || deviceMode == 12 || deviceMode == 13 || deviceMode == 15) {// Exit Setup Pages
    deviceMode = 6;
  return;           
  }
  if (deviceMode == 111 || deviceMode == 112 || deviceMode == 113) {// Don't save Month, Day or Year
    deviceMode = 11;
    return;            
  }
  if (deviceMode == 121) {// Don't save new time
    deviceMode = 12;
    return;            
  }
  if (deviceMode == 131 || deviceMode == 132 || deviceMode == 133 || deviceMode == 134) {// Don't save DST dates data
    deviceMode = 13;
    return;            
  }
  if (deviceMode == 14) {// Server Mode
    wifiServerOff();
    deviceMode = 6;
  return;            
  }
  if (deviceMode == 15) {// Sensor display Data
    deviceMode = 1;
  return;            
  }
}
void buttonF1(){                      // BUTTON (F1)
  // F1 =================== // F1 ============================== // F1 ======================== // F1 ========================= // F1 ============
  if (deviceMode == 5 || deviceMode == 6 || deviceMode == 7 || deviceMode == 14 || deviceMode == 15 || deviceMode == 111 || deviceMode == 112 ||deviceMode == 113 ||deviceMode == 121 ){return;}  
  if (deviceMode == 1 || deviceMode == 255 ) {  // Run Program #1
    progNum = 1;
    progCmd = 0;
    deviceMode = 4;
    lcdRefresh();
    return;
  }
  if (deviceMode == 2 || deviceMode == 4){   // Already in Countup mode.. Pause Toggle? (Any Key except STOP)
    programPause(1);
    return;
  }
  if (deviceMode == 3 || deviceMode == 32 || deviceMode == 33){  
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    // if (deviceSubMode == ANY) {// If ( UseRest = 1 ) { then set to 0 & Reset RestReg } else set UseRest to 1   ,  deviceSubMode to 1 }
    if ( useBonus == 0) { useBonus = 1;}
    deviceMode = 31;
    lcdRefresh();
    return;
  }
  if (deviceMode == 31){ // REST mode
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    // if (deviceSubMode == ANY) {// If ( UseRest = 1 ) { then set to 0 & Reset RestReg } else set UseRest to 1   ,  deviceSubMode to 1 }
    useBonus = 0;
    for (int i = 0; i < 2; ++i) { bonusRegister[i] = 0; }
    deviceMode = 3;
    lcdRefresh();
    return;
  }
  if (deviceMode == 11 ){
    deviceMode = 111;
    for (uint8_t i=0; i<4; i++){
      timeRegister[i] = 0;  
    }
    lcdSetup();
    return;
  }
  if (deviceMode == 12){
    if (timeZone == 'E') { timeZone = 'C';}
    else if (timeZone == 'C') {timeZone = 'M';} 
    else if (timeZone == 'M') {timeZone = 'W';} 
    else {timeZone = 'E';}
    eeprom.begin("prefs", false); // 'false' for read-write mode
    eeprom.putChar("timeZone", timeZone);
    eeprom.end();
    // configTime(TZOffset(), (daylightOffset_sec * DST * useDST), ntpServer);
    applyTimezone();
    lcdSetup();
    return;
  }
  if (deviceMode == 13 ){
    deviceMode = 131;
    for (uint8_t i=0; i<4; i++){
      timeRegister[i] = 0;  
    }
    lcdSetup();
    return;
  }
}
void buttonF2(){                      // BUTTON (F2)
  //F2 =================== //F2 ============================== //F2 ============================== //F2 ========================== //F2 ===============
  if (deviceMode == 5 || deviceMode == 6 || deviceMode == 7 || deviceMode == 14 || deviceMode == 15 || deviceMode == 111 || deviceMode == 112 ||deviceMode == 113 ||deviceMode == 121){return;} 
  if (deviceMode == 1  || deviceMode == 255 ){  // Run Program #2
    progNum = 2;
    progCmd = 0;
    deviceMode = 4;
    lcdRefresh();
    return;
  }
  if (deviceMode == 2 || deviceMode == 4){  // Already in Countup mode.. Pause Toggle? (Any Key except STOP)
    programPause(1);
    return;
  }
  if (deviceMode == 3 || deviceMode == 31 || deviceMode == 32 || deviceMode == 33){  
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    // if (deviceSubMode == ANY) {// If ( UseRest = 1 ) { then set to 0 & Reset RestReg } else set UseRest to 1   ,  deviceSubMode to 1 }
    if (blindBonus == 1){blindBonus = 0;} else {blindBonus = 1;}
    lcdRefresh();
    return;
  }  
  if (deviceMode == 11 ){
    deviceMode = 112;
    for (uint8_t i=0; i<4; i++){
      timeRegister[i] = 0;  
    }
    lcdSetup();
    return;
  }
  if (deviceMode == 12){
    // if (useDST){useDST = 0; DST= 0;} else {useDST = 1;}
    if (useDST){useDST = 0;} else {useDST = 1;}
    eeprom.begin("prefs", false); // 'false' for read-write mode
    eeprom.putBool("useDST", useDST);
    eeprom.end();
    //  if (useDST){
    //    getLocalTime(&timeinfo);           
    //    checkSetDST(timeinfo.tm_year+1900,timeinfo.tm_mon+1, timeinfo.tm_mday, timeinfo.tm_hour);
    //  }
    //  configTime(TZOffset(), (daylightOffset_sec * DST * useDST), ntpServer);
    applyTimezone();    
    lcdSetup();
    return;
  }
  if (deviceMode == 13 ){
    deviceMode = 132;
    for (uint8_t i=0; i<4; i++){
      timeRegister[i] = 0;  
    }
    lcdSetup();
    return;
  }
}
void buttonF3(){                      // BUTTON (F3)
  //F3 ========================= //F3 ========================= //F3 =================================== //F3 ====================== //F3 ==============
  if (deviceMode == 5 || deviceMode == 6 || deviceMode == 7 || deviceMode == 14 || deviceMode == 15 || deviceMode == 111 || deviceMode == 112 ||deviceMode == 113 ||deviceMode == 121){return;}
  if (deviceMode == 1  || deviceMode == 255 ){   // Run Program #3
    progNum = 3;
    progCmd = 0;
    deviceMode = 4;
    lcdRefresh();
    return;
  }
  if (deviceMode == 2 || deviceMode == 4){    // Already in Countup mode.. Pause Toggle? (Any Key except STOP)
    programPause(1);
    return;
  }
  if (deviceMode == 3 || deviceMode == 31 || deviceMode == 33){  
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    // if (deviceSubMode == ANY) {// If ( UseRest = 1 ) { then set to 0 & Reset RestReg } else set UseRest to 1   ,  deviceSubMode to 1 }
    if (useRest == 0){useRest = 1;}
    deviceMode = 32;
    return;
  }         
  if (deviceMode == 32){   
    useRest = 0;
    for (int i = 0; i < 4; ++i) { restRegister[i] = 0; }
    deviceMode = 3;
    lcdRefresh();
    return;
  }       
  if (deviceMode == 11 ){ // Set New Year!
    deviceMode = 113;
    for (uint8_t i=0; i<4; i++){
      timeRegister[i] = 0;  
    }
    lcdSetup();
    return;
  }
  if (deviceMode == 12){ // Set New Time!
    deviceMode = 121;
    for (uint8_t i=0; i<4; i++){
      timeRegister[i] = 0;  
    }
    lcdSetup();
    return;
  }
  if (deviceMode == 13 ){
    deviceMode = 133;
    for (uint8_t i=0; i<4; i++){
      timeRegister[i] = 0;  
    }
    lcdSetup();
    return;
  }
}
void buttonF4(){                      // BUTTON (F4)
  // F4 ========================= //F4 ======================= //F4 ======================== //F4 =============================== //F4 ==============
  if (deviceMode == 6 || deviceMode == 7 || deviceMode == 11 || deviceMode == 14 || deviceMode == 15 || deviceMode == 111 || deviceMode == 112 ||deviceMode == 113 ||deviceMode == 121){return;}
  if (deviceMode == 1  || deviceMode == 255 ){
    deviceMode = 5; // Change DeviceMode to 5: Program Select  
    return;
  }
  if (deviceMode == 2 || deviceMode == 4){   // Already in Countup mode.. Pause Toggle? (Any Key except STOP)
    programPause(1);
    return;
  } 
  if (deviceMode == 3 || deviceMode == 31 || deviceMode == 32){  
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    // if (deviceSubMode == ANY) {// If ( UseRest = 1 ) { then set to 0 & Reset RestReg } else set UseRest to 1   ,  deviceSubMode to 1 }
    if (useRounds == 0){ useRounds = 1;}
    if (rounds2Loops == 0) {rounds2Loops = 1;}
    deviceMode = 33;
    lcdRefresh();
    return;
  }
  if (deviceMode == 33){  
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    // if (deviceSubMode == ANY) {// If ( UseRest = 1 ) { then set to 0 & Reset RestReg } else set UseRest to 1   ,  deviceSubMode to 1 }
    useRounds = 0;
    rounds2Loops = 0;
    for (int i = 0; i < 2; ++i) { roundsRegister[i] = 0; }
    deviceMode = 3;
    lcdRefresh();
    return;
  }
  if (deviceMode == 5 ){  
    deviceMode = 6;// Device mode to "Setup or Settings mode
    return;
  }
  if(deviceMode == 12){
    if (worldTime){worldTime = 0;} else {worldTime = 1;}
    eeprom.begin("prefs", false); // 'false' for read-write mode
    eeprom.putBool("worldTime", worldTime);
    eeprom.end();
    lcdSetup();
    return;
  }
  if (deviceMode == 13 ){
    deviceMode = 134;
    for (uint8_t i=0; i<4; i++){
      timeRegister[i] = 0;  
    }
    lcdSetup();
    return;
  }  
}
void buttonNum(uint8_t userNumber){   // BUTTON (NUMBER)
  if (deviceMode == 1  || deviceMode == 255 ){  
    // Zero all manual entry registers and flags
    for (int i = 0; i < 4; ++i) { fightRegister[i] = 0; }
    for (int i = 0; i < 2; ++i) { bonusRegister[i] = 0; }
    useBonus = 0;
    for (int i = 0; i < 4; ++i) { restRegister[i] = 0; }
    useRest = 0;
    for (int i = 0; i < 2; ++i) { roundsRegister[i] = 0; }
    useRounds = 0;
    // process the pressed number into fight reg.
    // Register shifting not needed as it was just cleared and this will only run once.
    //fightRegister[0] = fightRegister[1];
    //fightRegister[1] = fightRegister[2];
    //fightRegister[2] = fightRegister[3];
    fightRegister[3] = userNumber;
    useFight = 1;
    // change to mode 3
    deviceMode = 3;
    lcdRefresh();
    return;
  }
  if (deviceMode == 2 || deviceMode == 4){    // Already in Countup mode.. Pause Toggle? (Any Key except STOP)
    programPause(1);
    return;
    }
  if (deviceMode == 3 ){  // fightReg Entry
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    // if (deviceSubMode == 0) {send convertKey(keyPress) to FightReg processor
    // if (deviceSubMode == 1) {send convertKey(keyPress) to RestReg processor
    // if (deviceSubMode == 2) {send convertKey(keyPress) to RoundsReg processor
    // if (deviceSubMode == 3) {send convertKey(keyPress) to BonusReg processor
    fightRegister[0] = fightRegister[1];
    fightRegister[1] = fightRegister[2];
    fightRegister[2] = fightRegister[3];
    fightRegister[3] = userNumber;
    lcdRefresh();
    return;
  }
  if (deviceMode == 31 ){  // BonusReg Entry
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    bonusRegister[0] = bonusRegister[1];
    bonusRegister[1] = userNumber;
    lcdRefresh();
    return;
  }
  if (deviceMode == 32 ){  // RestReg Entry
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    restRegister[0] = restRegister[1];
    restRegister[1] = restRegister[2];
    restRegister[2] = restRegister[3];
    restRegister[3] = userNumber;
    lcdRefresh();
    return;
  }
  if (deviceMode == 33 ){  // RoundsReg Entry
    // deviceSubMode ? Fight-30, Bonus-31, Rest-32, Rounds-33  along with UseRest, UseBonus, Use Blind flags?
    roundsRegister[0] = roundsRegister[1];
    roundsRegister[1] = userNumber;
    lcdRefresh();
    return;
  }
  if (deviceMode == 5){   
    if (userNumber != 1 && userNumber != 2 && userNumber != 3 ) { // Run program# SelectedProg and change to devicemode 4 Break;}
      progNum = userNumber;
      progCmd = 0;
      deviceMode = 4;
      lcdRefresh();
    }
    return;
  }
  if (deviceMode == 6){  // SETUP MODE TO GET INTO SUB MENU ==============================================================
    if (userNumber > 0 && userNumber < 7) deviceMode = userNumber + 10;
      //DO NOT break; // Needs to run new > 9 deviceMode "Setup" below!
  
    if (deviceMode == 11){ // Numbers do nothing in Set Date Mode
      return;
    }
    if (deviceMode == 12){ // Numbers do nothing in Set Clock Mode
      return;
    }
    if (deviceMode == 13) { // DST Date Settings
      return;     
    }
    if (deviceMode == 14) { // Server
      lcdSetup();
      if (!wifiServerStatus){wifiServerOn();}
      return;
    }
    if (deviceMode == 15) { // Sensor Value
      // Display sensor value and ignore all keys except STOP
      terminal.clear();
      return;
    }
    if (deviceMode == 16) { // Reboot
      ESP.restart();
    }
  }

  if (deviceMode == 111){ //  SUB, SUB SETTINGS MENU =====================================================================
    timeRegister[0] = timeRegister[1];
    timeRegister[1] = userNumber;
    lcdSetup();
    return;
  }
  if (deviceMode == 112){
    timeRegister[0] = timeRegister[1];
    timeRegister[1] = userNumber;
    lcdSetup();
    return;
  }
  if (deviceMode == 113){
    timeRegister[0] = timeRegister[1];
    timeRegister[1] = timeRegister[2];
    timeRegister[2] = timeRegister[3];
    timeRegister[3] = userNumber;
    lcdSetup();
    return;
  }
  if (deviceMode == 121){ // Set Clock SubMode
    timeRegister[0] = timeRegister[1];
    timeRegister[1] = timeRegister[2];
    timeRegister[2] = timeRegister[3];
    timeRegister[3] = userNumber;
    lcdSetup();
    return;
  }
  if (deviceMode == 131){ // DST Dates Start Sunday
    timeRegister[0] = userNumber;
    lcdSetup();
    return;
  }
  if (deviceMode == 132){ // DST Dates Start Month
    timeRegister[0] = timeRegister[1];
    timeRegister[1] = userNumber;
    lcdSetup();
    return;
  }
  if (deviceMode == 133){ // DST Dates End Sunday
    timeRegister[0] = userNumber;
    lcdSetup();
    return;
  }
  if (deviceMode == 134){ // DST Dates END Month
    timeRegister[0] = timeRegister[1];
    timeRegister[1] = userNumber;
    lcdSetup();
    return;
  }
}
void runDailyTasks(){
  if ( wifiSuccess == 0 ){
    if ( bootDelayWifiTimer.ended() && deviceMode == 1){ // ----------------------------------------------
      deviceMode = 0;
      SetupTime();
      deviceMode = 1;
    }
  } //-----------------------------------------------------------------------------------

  if(!getLocalTime(&timeinfo)){  return; }   // Don't run if there's no local time

  // MID DAY - Changes LCD display AM/PM. Note isn't needed in world time!
  if (timeinfo.tm_hour == 12 && timeinfo.tm_min == 0 && lastMiddayRunDay  != timeinfo.tm_mday) {
    lastMiddayRunDay  = timeinfo.tm_mday; // Will only run once per day
    //=======================
    if(deviceMode = 1) {lcdSetup();} 
    //=======================
  }

  // nightDisplayOff, ledDisplayState
  // 10PM to 6am SleepCheck DISPLAY OFF?
  if ( ( timeinfo.tm_hour >= prefsData.nightDisplayOffTime || timeinfo.tm_hour < 6) && deviceMode==255 && ledDisplayState == 1 && prefsData.useLightSensor && prefsData.nightDisplayOff ) {
    //=======================
    onboardled[0] = CRGB::Black;
    solidColorInstant(0, 575, 0);
    FastLED.show();
    ledDisplayState = 0; 
    //=======================
  }

  // MIDNIGHT screen rebuild for date changes
  if (timeinfo.tm_hour == 0 && timeinfo.tm_min == 0 && lastMidnightRunDay  != timeinfo.tm_mday) {
    lastMidnightRunDay  = timeinfo.tm_mday; // Will only run once per day
    //=======================
    if ( deviceMode == 1 ) { lcdSetup(); }
    //=======================
  }

  // 2AM Daily DST check and NTP sync
  if (timeinfo.tm_hour == prefsData.taskHour && timeinfo.tm_min == prefsData.taskMinute && lastTaskRunDay  != timeinfo.tm_mday) {
    lastTaskRunDay = timeinfo.tm_mday; // Will only run once per day
    //=======================
    deviceMode = 0;
    SetupTime();   // Sync with NTP server or set default time.
    if ( prefsData.useLightSensor ){
      deviceMode = 255; //deviceMode 255 "Dark Mode" to come out of in the AM sunlight.
      ledMode = 255; // So setup won't run back to dark mode and leave the LEDs black.
    }
    else {
      deviceMode = 1;
    }
    //=======================
  }

}
void runModeTasks(){
  if (deviceMode == 2){ // Count Up Mode
    timerU2.Timer();
  }
  if (deviceMode == 4){ // Program Running
    timerD1.Timer(); // always keeps timers updated for every program, anywhere.
    timerU2.Timer();
    timerD3.Timer();
    if (cmdCompleted && !programPaused){ // Starts with CMD 0. Previous CMD done, move onto next one UNLESS program is paused
      //Serial.print(progCmd);
      //Serial.print(" - ");
      //Serial.print(namespaces[progNum].charElements[progCmd]);
      //Serial.print(", ");
      //Serial.print(namespaces[progNum].byteElements[progCmd]);
      //Serial.println(" Done!"); 
      ++progCmd; // only have 0-29 CMDs per program.
      if (progCmd >= 30){resetProgramMode(); return;}
      cmdStarted = 0;
      cmdCompleted = 0;
    }
    switch (namespaces[progNum].charElements[progCmd]) {
    case 'A': // MACRO Action! Fight using registers calculating Rounds, Bonus and Blinds Bonus etc..
      action();
      break;
    case 'a': // Action Go! Starts what was set in Action MACRO
      actionGo();
      break;
    case 'B': // Set Bonus Registry
      bonus();
      break;
    case 'b': // Set blind 1 or 0 not blind
      blind();
      break;
    case 'C': // Commit to a Timer
      commitToTimer();
      break;
    case 'D': // Set Rest Time Min
      downTimeMin();
      break;
    case 'd': // Set rest Time Sec
      downTimeSec();
      break;    
    case 'E': // Run Effect # for effect time preset (or 5 seconds)
      effect();
      break;
    case 'e': // Set effects time
      effectTime();
      break;
    case 'F': // Set Fight MIN Registry and nothing else
      fightMin();
      break;
    case 'f': // Set Fight SEC Registry and nothing else
      fightSec();
      break;  
    case 'G': // Go Timers 1,2,3,12,13,23,123
      goTimers();
      break;
    case 'H': // Hammer Bell
      hammerBell();
      break;
    case 'I': // Set Rounds Interval
      setRoundsInterval();
      break;
    case 'i': // Set Rounds INC = 1 or DEC = 0
      setRoundsInc();
      break;
    case 'L': // Loop Head
      loopHead(); 
      break;
    case 'l': // Loop Tail
      loopTail();
      break;
    case 'M': // Meditate/ Rest MACRO SEC
      meditate(); 
      break;
    case 'N': // Nap TIme, Device do nothing for a while...
      napTime();
      break;
    case 'P': // Pause program on keypress or sensor
      userPause();
      break;
    case 'Q': // Reset all Program flags and return to Clock mode
      resetProgramMode();
      break;
    case 'R': // Set Rounds Registry
      setRounds();
      break;
    case 'r': // Execute Rounds values set by Interval, Inc/Dec
      RoundsExe();
      break;  
    case 'S': // Stop Timers
      stopTimers();
      break;
    case 'T': // Set LCD source and looks
      setDisplayT(namespaces[progNum].byteElements[progCmd]);
      break; 
    case 'U': // Set LED Display Top (Source and Looks)
      setDisplayU(namespaces[progNum].byteElements[progCmd]);
      break;
    case 'V': // Set LED Display bottom right (Source and Looks)
      setDisplayV(namespaces[progNum].byteElements[progCmd]);
      break;
    case 'W': // Set LED Display botttom left (Source and Looks)
      setDisplayW(namespaces[progNum].byteElements[progCmd]);
      break;
    case 'X': // set Timer ALL MIN REQUIRES you run x, y, z
      setTimerD1Min();
      break;
    case 'x': // set TimerD1 SEC
      setTimerD1Sec();
      break;
    case 'Y': // set Timer ALL MIN REQUIRES you run x, y, z
      setTimerU2Min();
      break;
    case 'y': // set TimerU2 SEC
      setTimerU2Sec();
      break;
    case 'Z': // set Timer ALL MIN REQUIRES you run x, y, z
      setTimerD3Min();
      break;
    case 'z': // set TimerD3 SEC
      setTimerD3Sec();
      break;
    
    default:
      break;
    }

  }

}
void runEveryMyCycle() {
  if (cycleFastTimer.ended()){ // Run Every 100 mS -----------------------------------------
    if (ledDisplayState != 0 ){
      // ledRefresh(); // Updates numbers on LED display (not color)
      if (prefsData.useLightSensor){
        if (deviceMode == 255){ // This math ensures dark mode will ALWAYS be able to go to brightness prefsData.brightnessMinDark.
        if (BRIGHTNESS > (brightnessTarget - prefsData.brightnessMin + prefsData.brightnessMinDark)) { BRIGHTNESS = --BRIGHTNESS ;}
        if (BRIGHTNESS < (brightnessTarget - prefsData.brightnessMin + prefsData.brightnessMinDark)) { BRIGHTNESS = ++BRIGHTNESS ;}
        }
        else { // Normal all modes LED brightness adjustments
        if (BRIGHTNESS > brightnessTarget) { BRIGHTNESS = --BRIGHTNESS ;}
        if (BRIGHTNESS < brightnessTarget) { BRIGHTNESS = ++BRIGHTNESS ;}
        }
      }    
      if (prefsData.useLightSensor == 0){ 
        BRIGHTNESS = prefsData.brightnessStatic; // Bypass all dimming using light sensor and set to min bright
      }
      FastLED.setBrightness(BRIGHTNESS);
    }
    //lcdRefresh();
    cycleFastTimer.set(100);
  } //-----------------------------------------------------------------------------------
  if (cycleMediumTimer.ended()){ // Run Every 250 mS ----------------------------------------
    if (myBacklightTimer.ended() && deviceMode != 15) { lcd.noBacklight(); } // True means times has reached zero, shut off backlight.
    runDailyTasks(); // This populates the timeInfo data struct from the realtime clock. MUST be run at least every second or clock display is effected.
    if (prefsData.useLightSensor){
      checkSensor();
      if (luxDelta > prefsData.luxDeltaCovered && sensorPressTimer.ended()){ // Virtual Keypress Detected!
        if (deviceMode == 2 || deviceMode == 4 ){ programPause(1); sensorPressTimer.set(3000); }
        lcd.backlight(); // Sensor triggered, turn on backlight and start timer over.
        myBacklightTimer.set(prefsData.lcdBacklightTime*1000);
      }
    }
    cycleMediumTimer.set(250);
  } //-----------------------------------------------------------------------------------
  if (cycleSlowTimer.ended()){ // Run Every 1000 mS ----------------------------------------------
    if ( prefsData.useLightSensor && luxRoomValid ) {  // If using Light Sensor with Stable Data... Determine LED display brightness changes and modes.

      if ( ledDisplayState == 1 ){ // CALC Brightness! Y = mX + b, This is the math (linear) to get what brightness display SHOULD be at (0-255);
      brightnessTarget = ( prefsData.brightnessSlope * luxSamples[0] ) + prefsData.brightnessLateral; 
      if (brightnessTarget > prefsData.brightnessMax) { brightnessTarget = prefsData.brightnessMax; }
      if (brightnessTarget < prefsData.brightnessMin) { brightnessTarget = prefsData.brightnessMin; }
      }

      if ( deviceMode == 1 ){ // Dark Mode ONLY engages in clock mode 1
        if ( luxSamples[0] < prefsData.luxDarkMode && myBacklightTimer.ended()) {deviceMode = 255; lcd.noBacklight();} // Go INTO Dark mode! LED display U goes red, V & W goes "off" to color 0 (black).
      }

      if ( deviceMode == 255 && luxSamples[0] > ( prefsData.luxDarkMode + 4) ) { // Out of Darkness! Normal clock LED display mode and/or COOL random LED effect here!?!  
        ledDisplayState = 1; 
        deviceMode = 1; 
        lcd.backlight(); 
        myBacklightTimer.set(prefsData.lcdBacklightTime*1000);
       } 
    }

    if (deviceMode == 1 || deviceMode == 2 || deviceMode == 255 ){neglected.set(prefsData.neglectTime*60000);} // While in clock, countup or dark mode always push neglect timer out.
    if (neglected.ended()){
      if (deviceMode == 4){resetProgramMode();} // Reset any running program!
      if (deviceMode == 14){wifiServerOff();} // Server on and neglected, turn off
      deviceMode = 1;
    }
    cycleSlowTimer.set(1000);
  }//-----------------------------------------------------------------------------------
}
void keyPressHandler(){
  // char keyPress = customKeypad.getKey(); // check for keypress from user.
  char keyPress = keypad.getKey();
  if (keyPress) { // IF there IS a keypress.. process it!
    if (prefsData.useBeeper){ 
      digitalWrite(BeeperPin, HIGH); 
      delay (100); //delay makes more beeper!
      digitalWrite(BeeperPin, LOW);
    } 
    lcd.backlight(); // User detected! Make sure the displays are lit no matter the hour!
    myBacklightTimer.set(prefsData.lcdBacklightTime*1000);
    if ( deviceMode == 255) { ledDisplayState = 1; ledSetup();}
    switch (keyPress) {
      case 'G': //START
        buttonStart();
        break; 
      case 'S': //STOP
        buttonStop();
        break;
      case 'A': //F1
        buttonF1();
        break;
      case 'B': //F2
        buttonF2();
        break;
      case 'C': //F3
        buttonF3();
        break;
      case 'D': //F4 
        buttonF4();
        break;
      case '9': // What's left MUST be a number!
      case '8':
      case '7':
      case '6':
      case '5':
      case '4':
      case '3':
      case '2':
      case '1':
      case '0': // ANY NUMBER DO:
        buttonNum(convertKey(keyPress));
        break;
      default:
        break;
    }
  }
}
//=========================================================================
void setup() {
  //Serial.begin(115200);
  Wire.begin(SDApin, SCLpin); // I2C comms bus
  lcd.backlight();
  terminal.begin();
  terminal.println("Booting...");
  // tell FastLED about the LED configurations
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  // FastLED.addLeds<LED_TYPE,18,COLOR_ORDER>(onboardled, 1).setCorrection(TypicalLEDStrip); //Onboard LED: Data pin 18, Num LEDS = 1
  FastLED.addLeds<NEOPIXEL, 18>(onboardled, 1); //Onboard LED: Data pin 18, Num LEDS = 1
  FastLED.setBrightness(prefsData.brightnessStatic); // set master brightness control
  onboardled[0] = CRGB::Red;
  ledDisplayState = 1;
  FastLED.show();
  cycleFastTimer.set(1); // REQUIRED! Ensures an expired clock to run on first instance.
  cycleMediumTimer.set(1);
  bootDelayWifiTimer.set(600000); //10 minutes = 600000 ms
  cycleSlowTimer.set(1);
  luxSampleTimer.set(1);
  napTimer.set(1);
  sensorPressTimer.set(1);
  neglected.set(1);
  specialEffectsTimer.set(1);
  keypad.begin();
  pinMode(BellPin, OUTPUT);
  pinMode(BeeperPin, OUTPUT);
  if (prefsData.useBeeper){(BeeperPin, HIGH); delay (100); digitalWrite(BeeperPin, LOW);}
  lcd.createChar(0, wifiSymbol); // Define custom character at index 0
  lcd.createChar(1, clockSymbol);
  lcd.createChar(2, blindSymbol);
  lcd.createChar(3, playSymbol);
  lcd.createChar(4, pauseSymbol);
  lcd.createChar(5, heartSymbol);
  lcd.createChar(6, bellSymbol);
  initializePreferences();
  lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE, 0x23, &Wire); // Sets ADDRESS of light meter found via I2C scanner in library example.
  LittleFS.begin(true);
    // --- Creates a static CSS file Route in the server BEFORE it begins. server.off will NOT remove the route.---
    // File MUST be uploaded seperately FIRST! 1) style.css must be in project/data folder. 2) PlatformIO > PROJECT TASKS > ESP32-s2-saola-1 > Platform > Upload Filesystem Image 
  server.on("/style.css", HTTP_GET, []() {
    File file = LittleFS.open("/style.css", "r");
    if (!file) {
      server.send(404, "text/plain", "CSS file not found");
      return;
    }
    server.sendHeader("Cache-Control", "max-age=86400");
    server.streamFile(file, "text/css");
    file.close();
  });     // MUST ADD TO HTML pages     <link rel="stylesheet" href="/style.css">
  deviceMode = 0;
  SetupTime();   // Sync with NTP server or set default time.
  resetProgramMode(); // This clears all program assets & sets deviceMode = 1 (clock);
  lcdSetup();
  myBacklightTimer.set(prefsData.lcdBacklightTime*1000);

}
void loop() {
  if (wifiServerStatus){server.handleClient();} // if Webserver lit.. Continuously handle incoming client requests to the web server
  runEveryMyCycle();
  runModeTasks();
  keyPressHandler(); //Constantly checks for user keypress to process.
  if (ledDisplayState != 0 ){
    ledRefresh(); // Updates number values on LED display (not color)
    fastLEDRefresh(); // Updates colors with masks
  }
  lcdRefresh(); // Updates the LCD fast so hiccups aren't present.
}

/* INFO: ===========  Colors, Device Mode List & Code Examples to retrieve Time Values from internal clock

Preference Storage Usage
Type	    Store	        Retrieve
bool      putBool()     getBool()
uint8_t	  putUChar()	  getUChar()
int8_t	  putChar()	    getChar()
uint16_t	putUShort()	  getUShort()
uint32_t	putUInt()	    getUInt()
uint64_t	putULong64()	getULong64()
float	    putFloat()	  getFloat()
double	  putDouble()	  getDouble()
String	  putString()	  getString()
=======================================================
LED Addresses
  // D0:0-40(41), D1:41-81(41), D2:84-124(41), D3:125-165(41), D4:166-203(38), D5:204-241(38), D6:242-324(83), D7:325-407(83), D8:410-492(83), D9:493-575(83)
  // LC:82-83(2), UC:408-409(2)
  // U(106) = 242-575(334), V(103) = 0-165(166), W(104) = 166-241(76)

0: Black
1: Red
2: Orange
3: Yellow
4: Green
5: Dark Green
6: Teal
7: Cyan
8: Blue
9: Indigo
10: Purple
11: Magenta
12: Pink
13: White
14: Rainbow
15: Rainbow with Glitter
16: Confetti
// 13: Dark Gray
// 14: Medium Gray
// 15: Light Gray
// 16: White
=======================================================
Device Modes:
0-Boot Terminal
1-Clock
2-CntUp
3-ManEntry
4-Prog
5-ProgSel
6-SetupSel

11-Date Settings
12-Clock Settings
13-DST Dates Settings
14-Server On
15-Sensor Data> Change to Terminal Device Mode 0 and set sensorDataOutput = 1
16-Reboot

30-ManEntry(Fight)?
31-ManEntry(Bonus)
32-ManEntry(Rest)
33-ManEntry(Rounds)

111-Setup(Month)
112-Setup(Day)
113-Setup(Year)

121-Clock Time Entry

131-DST Start Sunday
132-DST Start Month
133-DST End Month
134-DST End Month

201-
202-
203-
204-
205-
206-
207-

255-Dark Mode
=======================================================
Example: 
if (worldTime){lcd.print(&timeinfo, "%H:%M:%S");} else {lcd.print(&timeinfo, "%I:%M:%S");} //%p am/pm

https://cplusplus.com/reference/ctime/strftime/
  %A	returns day of week
  %B	returns month of year
  %d	returns day of month
  %Y	returns year
  %H	returns hour
  %M	returns minutes
  %S	returns seconds
  %b    abbreviated month name
  %a    abbreviated weekday name 
  %U    week number with the first Sunday as the first day of week one.

%a	Abbreviated weekday name *	Thu
%A	Full weekday name * 	Thursday
%b	Abbreviated month name *	Aug
%B	Full month name *	August
%c	Date and time representation *	Thu Aug 23 14:55:02 2001
%C	Year divided by 100 and truncated to integer (00-99)	20
%d	Day of the month, zero-padded (01-31)	23
%D	Short MM/DD/YY date, equivalent to %m/%d/%y	08/23/01
%e	Day of the month, space-padded ( 1-31)	23
%F	Short YYYY-MM-DD date, equivalent to %Y-%m-%d	2001-08-23
%g	Week-based year, last two digits (00-99)	01
%G	Week-based year	2001
%h	Abbreviated month name * (same as %b)	Aug
%H	Hour in 24h format (00-23)	14
%I	Hour in 12h format (01-12)	02
%j	Day of the year (001-366)	235
%m	Month as a decimal number (01-12)	08
%M	Minute (00-59)	55
%n	New-line character ('\n')	
%p	AM or PM designation	PM
%r	12-hour clock time *	02:55:02 pm
%R	24-hour HH:MM time, equivalent to %H:%M	14:55
%S	Second (00-61)	02
%t	Horizontal-tab character ('\t')	
%T	ISO 8601 time format (HH:MM:SS), equivalent to %H:%M:%S	14:55:02
%u	ISO 8601 weekday as number with Monday as 1 (1-7)	4
%U	Week number with the first Sunday as the first day of week one (00-53)	33
%V	ISO 8601 week number (01-53)	34
%w	Weekday as a decimal number with Sunday as 0 (0-6)	4
%W	Week number with the first Monday as the first day of week one (00-53)	34
%x	Date representation *	08/23/01
%X	Time representation *	14:55:02
%y	Year, last two digits (00-99)	01
%Y	Year	2001
%z	ISO 8601 offset from UTC in timezone (1 minute=1, 1 hour=100)
If timezone cannot be determined, no characters	+100
%Z	Timezone name or abbreviation *
If timezone cannot be determined, no characters	CDT
%%	A % sign	%


Compiles but makes ? numbers 
  int foo = timeinfo.tm_year;
  Serial.println(foo);

struct tm
{
  int	tm_sec;
  int	tm_min;
  int	tm_hour;
  int	tm_mday;
  int	tm_mon;
  int	tm_year;
  int	tm_wday;
  int	tm_yday;
  int	tm_isdst;
#ifdef __TM_GMTOFF
  long	__TM_GMTOFF;
#endif
#ifdef __TM_ZONE
  const char *__TM_ZONE;
#endif
};

SOLVED PROBLEMS ================================
PROBLEM: (fixed?) LED Light levels for LED brightness in normal mode displayState = 3 and Red mode displayState = 1    not started 
  Midnight> displayState: 0 wake on light level above dark mode (i.e. morning) to return to display mode 3 (dimming curve)
  Have a variance around ambient (current) LUX, have vectored step rate to lag behind the delta to land in the variance and stop.
PROBLEM: (Fixed but needs clean up of old serial code) LCD screen "Console" Mode 0 writes are sloppy and overwriting themselves, Need a scrolling "Monitor style" code.
PROBLEM: (fixed? line 1648) when clock rolls over at midnight the LED time is 0:00 despite being in AM/PM mode! LCD shows 12:00 correctly.
PROBLEM: (fixed? line 1877) While in Setup the clock LED display isn't running.
PROBLEM: (Fixed? May need shorter delay) Sensor pause needs a button needs delay.
PROBLEM: (Fixed) Pauses LED display Purple etc. return colors are still messed up and onboard LED in countup might be wrong
PROBLEM: (fixed?) Manual Program: Less bell (relay usage) and internal delays (poor user interface)! 
PROBLEM: (Fixed?) Need function to check wifi and NTP status to set onboard LED, after program, this has to be checked to be re-set.
PROBLEM: (SET) Onboard LED color codes? (Blue-OK), (Yellow-Wifi/NTP problem), (Orange- Default Program Color), (Green- Go in program), (Red-Stop in program), (Purple-Paused)
PROBLEM: (Fixed) "previousSecond variable when checking time needed? now that it's on a tinyTimer"
PROBLEM: (Fixed) Clock Seconds are now only displayed every 2 seconds!
PROBLEM: (fixed?)"Sleep Mode" in DailyTasks needs to be built!
PROBLEM: (fixed?) Stored programs default to Q0 for every CMD.
PROBLEM: (fixed?) LCD and LED clock Time no longer updates hrs, mn, secs..!!!!
PROBLEM: (fixed) lux Readings are erratic at high end. luxStabilityVariance = 5 lux, will not work up here! What about (luxDelta) set % of measured value ?
PROBLEM: (fixed) SENSOR data mode need to continue in (AND out of) Dark Mode for calibration needs! Terminal readouts STOP in dark mode!
PROBLEM: (Fixed) Brightness value doesn't sem to be effecting the actual LED brightness?
PROBLEM: (fixed) Timer to return to clock mode, if forgotten in a sub mode?
PROBLEM: (fixed) Add bool "useSensor" & brightnessManual (value)? if sensor died... Whole device would be worthless!
PROBLEM: (fixed) REALLY need the brightnessStepRate to be 1 !! Does this look OK? @250ms rate (a 0 to 50 slew in 12 seconds? 150 to 0 in 37 sec) @100ms rate (a 0 to 50 slew in 5 seconds? 150 to 0 in 15 sec) 
PROBLEM: (fixed) Set all program intitial values to "Q, 0" NOT A-Z, 0-30 etc..
PROBLEM: (fixed) Sleep mode didn't work, display was dim red w/ day lit up like in sensor mode wrong
PROBLEM: (fixed) Get rid of "grey" colors! At dim levels they look terrible.
PROBLEM: (fixed) Dark mode below darkLux is broken!
PROBLEM: (fixed)Countup DM2 - lower right (section V) is broken.
PROBLEM: Fix tips of LED digits to look better?
PROBLEM: (non-critical) Convert prefs webpage to use numbers instead of Strings? To speed everything up?
PROBLEM: (non-critical) Remove Field Items from Webpages?  Needed? How will items stack? <br> ?
PROBLEM: (fixed?)LCD backlight shuts off after boot? Why?
PROBLEM: (fixed?) Brightness room shifts seem too long to respond and then (also maybe) too slow to fade??
PROBLEM: (fixed?) Key press invoked "Out of Darkness" should last for a longer time (like LCD backlight timer?), else dark room renders device useless!
PROBLEM: (fixed?) Out of darkness via light level is broken YES!
PROBLEM: (fixed?) Sleep mode should use "use sensor" to not activate
PROBLEM: (fixed?) Sleep "wakeup" via light level is broken?
PROBLEM: (fixed?) LCD Time looks broken (slow, inconsistent) needs faster refresh.
PROBLEM: (fixed?) IF Program has no Q at end! (BAD!) Add a failsafe CMD counter <=30 ? 
PROBLEM: (fixed?) If Expiremental handleprefs num type and checkboxes works, convert ALL!
Problem: (fixed?) If a Display source is set to t timer but commited to a different one in the program... That timer's .timer is never run to update it! Should this be run in the refresh LED if source is set to that timer?]
NOTES: If random keypad reboot continue.. Adjust scanIntervalMs in ESP32MatrixKeypad.h higher until keypad is sluggish, that's your ceiling of how fast to "hammer" the keypad GPIO pins.
PROBLEM: (Bytes in loop all vary constrained 0-255) Floats an use realistic clamping? Error checking on SubmitPreference? example.. example: Only change if 1-255 etc..
PROBLEM: (Done?)Settings changeable in webserver not finished.
PROBLEM: CMDS and program call buttons not finished and tested.
PROBLEM: (No way around it) In Program... Meditate function (when there in NO REST) flashes the display RED! Looks broken to user. It's IN the "auto" program just before M is called!
PROBLEM: Flesh out / test and add special effects!
PROBLEM: Optimize fastLEDRefresh()!
PROBLEM: useDigitMask is for the entire display! What about an effect you want all to show but have data in another section? (Also in the E CMD?)

*/