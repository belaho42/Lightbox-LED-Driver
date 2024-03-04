#include "FastLED.h"

#define NUM_LEDS   48
#define LED_TYPE   WS2811
#define COLOR_ORDER   GRB
#define DATA_PIN        5
#define VOLTS          5
#define MAX_MA       1500

//  TwinkleFOX: Twinkling 'holiday' lights that fade in and out.
//  Colors are chosen from a palette; a few palettes are provided.
//
//  The idea behind this (new) implementation is that there's one
//  basic, repeating pattern that each pixel follows like a waveform:
//  The brightness rises from 0..255 and then falls back down to 0.
//  The brightness at any given point in time can be determined as
//  as a function of time, for example:
//    brightness = sine(time); // a sine wave of brightness over time
//
//  So the way this implementation works is that every pixel follows
//  the exact same wave function over time.  In this particular case,
//  I chose a sawtooth triangle wave (triwave8) rather than a sine wave,
//  but the idea is the same: brightness = triwave8(time).  
//  
//  Of course, if all the pixels used the exact same wave form, and 
//  if they all used the exact same 'clock' for their 'time base', all
//  the pixels would brighten and dim at once -- which does not look
//  like twinkling at all.
//
//  So to achieve random-looking twinkling, each pixel is given a 
//  slightly different 'clock' signal.  Some of the clocks run faster, 
//  some run slower, and each 'clock' also has a random offset from zero.
//  The net result is that the 'clocks' for all the pixels are always out 
//  of sync from each other, producing a nice random distribution
//  of twinkles.
//
//  The 'clock speed adjustment' and 'time offset' for each pixel
//  are generated randomly.  One (normal) approach to implementing that
//  would be to randomly generate the clock parameters for each pixel 
//  at startup, and store them in some arrays.  However, that consumes
//  a great deal of precious RAM, and it turns out to be totally
//  unnessary!  If the random number generate is 'seeded' with the
//  same starting value every time, it will generate the same sequence
//  of values every time.  So the clock adjustment parameters for each
//  pixel are 'stored' in a pseudo-random number generator!  The PRNG 
//  is reset, and then the first numbers out of it are the clock 
//  adjustment parameters for the first pixel, the second numbers out
//  of it are the parameters for the second pixel, and so on.
//  In this way, we can 'store' a stable sequence of thousands of
//  random clock adjustment parameters in literally two bytes of RAM.
//
//  There's a little bit of fixed-point math involved in applying the
//  clock speed adjustments, which are expressed in eighths.  Each pixel's
//  clock speed ranges from 8/8ths of the system clock (i.e. 1x) to
//  23/8ths of the system clock (i.e. nearly 3x).
//
//  On a basic Arduino Uno or Leonardo, this code can twinkle 300+ pixels
//  smoothly at over 50 updates per seond.
//
//  -Mark Kriegsman, December 2015

#define INFO

// Print info to serial or not
// #define DEBUG1
#ifdef INFO
  #define INFO_PRINT(x)  Serial.println (x)
#else
  #define INFO_PRINT(x)
#endif

CRGBArray<NUM_LEDS> leds;

// Overall twinkle speed.
// 0 (VERY slow) to 8 (VERY fast).  
// 4, 5, and 6 are recommended, default is 4.
#define TWINKLE_SPEED 3

// Overall twinkle density.
// 0 (NONE lit) to 8 (ALL lit at once).  
// Default is 5.
// #define TWINKLE_DENSITY 5
#define TWINKLE_DENSITY 8

// How often to change color palettes.
#define SECONDS_PER_PALETTE  20
// Also: toward the bottom of the file is an array 
// called "ActivePaletteList" which controls which color
// palettes are used; you can add or remove color palettes
// from there freely.

// Background color for 'unlit' pixels
// Can be set to CRGB::Black if desired.
CRGB gBackgroundColor = CRGB::Black; 
// CRGB gBackgroundColor = CRGB::Parchment; 
// Example of dim incandescent fairy light background color
// CRGB gBackgroundColor = CRGB(CRGB::FairyLight).nscale8_video(16);

// If AUTO_SELECT_BACKGROUND_COLOR is set to 1,
// then for any palette where the first two entries 
// are the same, a dimmed version of that color will
// automatically be used as the background color.
#define AUTO_SELECT_BACKGROUND_COLOR 0

// If COOL_LIKE_INCANDESCENT is set to 1, colors will 
// fade out slighted 'reddened', similar to how
// incandescent bulbs change color as they get dim down.
#define COOL_LIKE_INCANDESCENT 0

CRGBPalette16 gCurrentPalette;
CRGBPalette16 gTargetPalette;

void setup() {
  // Enable serial
  Serial.begin(9600);

  // Report setup beginning
  INFO_PRINT("Beginning setup");

  delay(3000); //safety startup delay
  FastLED.setMaxPowerInVoltsAndMilliamps(VOLTS, MAX_MA);
  FastLED.addLeds<LED_TYPE,DATA_PIN,COLOR_ORDER>(leds, NUM_LEDS)
    .setCorrection(TypicalLEDStrip);

  chooseNextColorPalette(gTargetPalette);

  INFO_PRINT("Setup complete\n\n");
}


void loop()
{
  // Serial.println(printPalette(gCurrentPalette));

  EVERY_N_SECONDS(SECONDS_PER_PALETTE) { 
    chooseNextColorPalette(gTargetPalette);
  }
  
  EVERY_N_MILLISECONDS(10) {
    nblendPaletteTowardPalette(gCurrentPalette, gTargetPalette, 12);
  }

  drawTwinkles(leds);
  
  FastLED.show();
}


//  This function loops over each pixel, calculates the 
//  adjusted 'clock' that this pixel should use, and calls 
//  "CalculateOneTwinkle" on each pixel.  It then displays
//  either the twinkle color of the background color, 
//  whichever is brighter.
void drawTwinkles(CRGBSet& L)
{
  // "PRNG16" is the pseudorandom number generator
  // It MUST be reset to the same starting value each time
  // this function is called, so that the sequence of 'random'
  // numbers that it generates is (paradoxically) stable.
  uint16_t PRNG16 = 11337;
  
  uint32_t clock32 = millis();

  // Set up the background color, "bg".
  // if AUTO_SELECT_BACKGROUND_COLOR == 1, and the first two colors of
  // the current palette are identical, then a deeply faded version of
  // that color is used for the background color
  CRGB bg;
  if((AUTO_SELECT_BACKGROUND_COLOR == 1) &&
      (gCurrentPalette[0] == gCurrentPalette[1])) {
    bg = gCurrentPalette[0];
    uint8_t bglight = bg.getAverageLight();
    if(bglight > 64) {
      bg.nscale8_video(16); // very bright, so scale to 1/16th
    } else if(bglight > 16) {
      bg.nscale8_video(64); // not that bright, so scale to 1/4th
    } else {
      bg.nscale8_video(86); // dim, scale to 1/3rd.
    }
  } else {
    bg = gBackgroundColor; // just use the explicitly defined background color
  }

  uint8_t backgroundBrightness = bg.getAverageLight();
  
  for(CRGB& pixel: L) {
    PRNG16 = (uint16_t)(PRNG16 * 2053) + 1384; // next 'random' number
    uint16_t myclockoffset16= PRNG16; // use that number as clock offset
    PRNG16 = (uint16_t)(PRNG16 * 2053) + 1384; // next 'random' number
    // use that number as clock speed adjustment factor (in 8ths, from 8/8ths to 23/8ths)
    uint8_t myspeedmultiplierQ5_3 =  ((((PRNG16 & 0xFF)>>4) + (PRNG16 & 0x0F)) & 0x0F) + 0x08;
    uint32_t myclock30 = (uint32_t)((clock32 * myspeedmultiplierQ5_3) >> 3) + myclockoffset16;
    uint8_t  myunique8 = PRNG16 >> 8; // get 'salt' value for this pixel

    // We now have the adjusted 'clock' for this pixel, now we call
    // the function that computes what color the pixel should be based
    // on the "brightness = f(time)" idea.
    CRGB c = computeOneTwinkle(myclock30, myunique8);

    uint8_t cbright = c.getAverageLight();
    int16_t deltabright = cbright - backgroundBrightness;
    if(deltabright >= 32 || (!bg)) {
      // If the new pixel is significantly brighter than the background color, 
      // use the new color.
      pixel = c;
    } else if(deltabright > 0) {
      // If the new pixel is just slightly brighter than the background color,
      // mix a blend of the new color and the background color
      pixel = blend(bg, c, deltabright * 8);
    } else { 
      // if the new pixel is not at all brighter than the background color,
      // just use the background color.
      pixel = bg;
    }
  }
}


//  This function takes a time in pseudo-milliseconds,
//  figures out brightness = f(time), and also hue = f(time)
//  The 'low digits' of the millisecond time are used as 
//  input to the brightness wave function.  
//  The 'high digits' are used to select a color, so that the color
//  does not change over the course of the fade-in, fade-out
//  of one cycle of the brightness wave function.
//  The 'high digits' are also used to determine whether this pixel
//  should light at all during this cycle, based on the TWINKLE_DENSITY.
CRGB computeOneTwinkle(uint32_t ms, uint8_t salt)
{
  uint16_t ticks = ms >> (8-TWINKLE_SPEED);
  uint8_t fastcycle8 = ticks;
  uint16_t slowcycle16 = (ticks >> 8) + salt;
  slowcycle16 += sin8(slowcycle16);
  slowcycle16 =  (slowcycle16 * 2053) + 1384;
  uint8_t slowcycle8 = (slowcycle16 & 0xFF) + (slowcycle16 >> 8);
  
  uint8_t bright = 0;
  if(((slowcycle8 & 0x0E)/2) < TWINKLE_DENSITY) {
    bright = attackDecayWave8(fastcycle8);
  }

  uint8_t hue = slowcycle8 - salt;
  CRGB c;
  if(bright > 0) {
    c = ColorFromPalette(gCurrentPalette, hue, bright, NOBLEND);
  } else {
    c = CRGB::Black;
  }
  return c;
}


// This function is like 'triwave8', which produces a 
// symmetrical up-and-down triangle sawtooth waveform, except that this
// function produces a triangle wave with a faster attack and a slower decay:
//
//     / \ 
//    /     \ 
//   /         \ 
//  /             \ 
//

uint8_t attackDecayWave8(uint8_t i)
{
  if(i < 86) {
    return i * 3;
  } else {
    i -= 86;
    return 255 - (i + (i/2));
  }
}

// A mostly (dark) green palette with red berries.
#define Holly_Green 0x00580c
#define Holly_Red   0xB00402
const TProgmemRGBPalette16 p_0 FL_PROGMEM =
{  Holly_Green, Holly_Green, Holly_Red, Holly_Green, 
   Holly_Red, Holly_Green, Holly_Green, Holly_Green, 
   Holly_Green, Holly_Green, Holly_Red, Holly_Green, 
   Holly_Green, Holly_Green, Holly_Green, Holly_Red 
};

#define Purple 0x800080
#define Orange 0xFF8C00
const TProgmemRGBPalette16 p_1 FL_PROGMEM =
{   Orange, Holly_Green, Purple, Holly_Green, Holly_Green, Purple,
    Orange, Holly_Green, Purple, Holly_Green, Orange, Purple,
    Orange, Holly_Green, Purple, Holly_Green
};

#define MediumBlue 0x0000CD
#define Red 0xFF0000
const TProgmemRGBPalette16 p_2 FL_PROGMEM =
{   MediumBlue, Red, MediumBlue, Red, MediumBlue,
    MediumBlue, Red, MediumBlue, Red, MediumBlue,
    MediumBlue, Red, MediumBlue, Red, MediumBlue,
    Red
};

#define Yellow 0xFFFF00
const TProgmemRGBPalette16 p_3 FL_PROGMEM =
{   MediumBlue, Red, Yellow, Red, MediumBlue,
    MediumBlue, Red, Yellow, Red, MediumBlue,
    MediumBlue, Red, Yellow, Red, MediumBlue,
    Red
};

const TProgmemRGBPalette16 p_4 FL_PROGMEM =
{   Yellow, Yellow, Purple, Purple, Purple,
    Yellow, Yellow, Yellow, Purple, Purple,
    Yellow, Yellow, Purple, Purple, Purple
};

#define Green 0x008000
#define Teal 0x00FFFF
#define Blue 0x0000FF
const TProgmemRGBPalette16 p_5 FL_PROGMEM =
{   Green, Green, Yellow, Blue, Blue,
    Green, Green, Yellow, Blue, Blue,
    Green, Green, Yellow, Blue, Blue,
    Green    
};

const TProgmemRGBPalette16 p_6 FL_PROGMEM =
{   0x0000FF, 0x00FF00, 0x00FFFF, 0xFF0000, 0xFF00FF, 0xFFFF00,
    0x0000FF, 0x00FF00, 0x00FFFF, 0xFF0000, 0xFF00FF, 0xFFFF00,
    0x0000FF, 0x00FF00, 0xFF0000, 0xFF0000
};

// Add or remove palette names from this list to control which color
// palettes are used, and in what order.
const TProgmemRGBPalette16* ActivePaletteList[] = {
  &p_0,   // green+red
  &p_1,   // green+purple+orange
  &p_2,   // red+blue
  &p_4,   // yellow+purple
  &p_5,   // Green+blue + some yellow
  &p_6    // primary & secondary colors
  // add a green heavy palette
  // try an RGB
};

// this variable is used to identify the current palette.
// if you change the palettes at all, you'll need to 
// change the corresponding entry in this variable
char *palNames[] = {"green+red", 
                    "green+purple+orange", 
                    "red+blue",
                    "yellow+purple", 
                    "green+blue + some yellow", 
                    "pri+sec"};


// Advance to the next color palette in the list (above).
void chooseNextColorPalette(CRGBPalette16& pal)
{
  const uint8_t numberOfPalettes = sizeof(ActivePaletteList) / sizeof(ActivePaletteList[0]);
  static uint8_t whichPalette = -1; 
  whichPalette = addmod8(whichPalette, 1, numberOfPalettes);

  const int next = int(uint8_t(whichPalette));

  pal = *(ActivePaletteList[whichPalette]);
  Serial.print("Next palette is ");
  Serial.print(next);
  Serial.print(" which is ");
  Serial.println(palNames[whichPalette]);
}