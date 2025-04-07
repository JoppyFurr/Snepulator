/*
 * Snepulator
 * MIDI Player header.
 */

typedef struct MIDI_Player_Context_s {

    uint8_t *midi;
    uint32_t midi_size;
    uint32_t index;
    uint32_t delay; /* Number of time intervals to delay before processing the next event */

    /* Values read from MIDI header */
    uint32_t format;
    uint32_t n_tracks;
    uint32_t tick_div;

    YM2413_Context *ym2413_context;
    uint32_t ym2413_clock;
    uint64_t ym2413_millicycles; /* Remaining time to carry over to the next run of the chip. */

    /* Visualisation */
    VGM_Player_Context vgm_player_context;

} MIDI_Player_Context;

/* Initialize the MIDI Player */
MIDI_Player_Context *midi_player_init (void);
