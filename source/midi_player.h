/*
 * Snepulator
 * MIDI Player header.
 */

typedef enum MIDI_Expect_e {
    EXPECT_TRACK_HEADER = 0,
    EXPECT_DELTA_TIME,
    EXPECT_EVENT
} MIDI_Expect;


typedef struct MIDI_Player_Context_s {

    uint8_t *midi;
    uint32_t midi_size;

    /* Playback state */
    uint32_t index;
    uint32_t track_end;     /* Index of the first byte outside of the current track */
    uint32_t tick_length;   /* Number of NTSC Colourburst clocks per MIDI tick */
    uint32_t clocks;        /* Unspent NTSC Colourburst clock cycles */
    uint32_t delay;         /* Number of time intervals to delay before processing the next event */
    uint8_t status;         /* Status byte for running events */
    MIDI_Expect expect;     /* Next expected element in track */

    /* Channel state */
    uint8_t channel_program [16];

    /* Values read from MIDI header */
    uint32_t format;
    uint32_t n_tracks;
    uint32_t tick_div;

    /* MIDI state */
    uint32_t tempo;         /* µs per quarter-note */

    YM2413_Context *ym2413_context;
    uint32_t ym2413_clock;
    uint64_t ym2413_millicycles; /* Remaining time to carry over to the next run of the chip. */

    /* Visualisation */
    VGM_Player_Context vgm_player_context;

} MIDI_Player_Context;

/* Initialize the MIDI Player */
MIDI_Player_Context *midi_player_init (void);
