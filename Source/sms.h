/*
 * Sega Master System
 */

#define SMS_CLOCK_RATE_PAL  3546895
#define SMS_CLOCK_RATE_NTSC 3579545

typedef struct SMS_Gamepad_t {
    bool up;
    bool down;
    bool left;
    bool right;
    bool button_1;
    bool button_2;
} SMS_Gamepad;

typedef enum SMS_Region_t {
    REGION_WORLD,
    REGION_JAPAN
} SMS_Region;



/* Returns the SMS clock-rate in Hz. */
uint32_t sms_get_clock_rate ();

/* Callback to supply SDL with audio frames. */
void sms_audio_callback (void *userdata, uint8_t *stream, int len);

/* Emulate the SMS for the specified length of time. */
void sms_run (double ms);

/* Reset the SMS and load a new BIOS and/or cartridge ROM. */
void sms_init (char *bios_filename, char *cart_filename);

/* Returns true if there is a non-maskable interrupt. */
bool sms_nmi_check ();
