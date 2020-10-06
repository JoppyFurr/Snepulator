/*
 * Sega Master System
 */

#define SMS_CLOCK_RATE_PAL  3546895
#define SMS_CLOCK_RATE_NTSC 3579545

typedef enum SMS_Mapper_e {
    SMS_MAPPER_UNKNOWN = 0,
    SMS_MAPPER_SEGA,
    SMS_MAPPER_CODEMASTERS,
    SMS_MAPPER_KOREAN,
} SMS_Mapper;

/* Reset the SMS and load a new BIOS and/or cartridge ROM. */
void sms_init ();
