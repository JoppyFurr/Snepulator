/*
 * Sega Master System
 */

#define SMS_CLOCK_RATE_PAL  3546895
#define SMS_CLOCK_RATE_NTSC 3579545

typedef enum SMS_Region_t {
    REGION_WORLD,
    REGION_JAPAN
} SMS_Region;

/* Reset the SMS and load a new BIOS and/or cartridge ROM. */
void sms_init ();
