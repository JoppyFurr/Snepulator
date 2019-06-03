
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

typedef enum SMS_Framerate_t {
    FRAMERATE_NTSC,
    FRAMERATE_PAL
} SMS_Framerate;

uint32_t sms_get_clock_rate ();

void sms_audio_callback (void *userdata, uint8_t *stream, int len);
void sms_run (double ms);
void sms_init (char *bios_filename, char *cart_filename);
bool sms_nmi_check ();
