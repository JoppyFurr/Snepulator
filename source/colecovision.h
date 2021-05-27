/*
 * Sega SG-1000
 */

#define COLECOVISION_CLOCK_RATE_PAL  3546895
#define COLECOVISION_CLOCK_RATE_NTSC 3579545

/* Stores the controller interface state */
typedef enum ColecoVision_Input_Mode_e {
    COLECOVISION_INPUT_MODE_JOYSTICK,
    COLECOVISION_INPUT_MODE_KEYPAD,
} ColecoVision_Input_Mode;

/* Reset the ColecoVision and load a new cartridge ROM. */
void colecovision_init ();
