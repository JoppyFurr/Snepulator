
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

void sms_init (char *bios_filename, char *cart_filename);
void sms_run_frame (void);
