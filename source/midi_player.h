/*
 * Snepulator
 * MIDI Player header.
 */

#define MIDI_YM2413_COUNT 3
#define MIDI_SYNTH_QUEUE_SIZE 32
#define MIDI_RHYTHM_QUEUE_SIZE 8

typedef enum MIDI_Expect_e {
    EXPECT_DELTA_TIME = 0,
    EXPECT_EVENT
} MIDI_Expect;


/*
 * synth_id definition:
 *
 * Eight bits to describe the location of the ym2413 channel being used:
 *
 *  Bits [7:5] - Chip number
 *  Bit  [  4] - 0: Melody patch, 1: Rhythm patch
 *  Bits [3:0] - Melody channel number, or Rhythm instrument number.
 */
#define SYNTH_ID_CHIP_MASK    0xe0
#define SYNTH_ID_RHYTHM_BIT   0x10
#define SYNTH_ID_CHANNEL_MASK 0x0f

typedef struct MIDI_Channel_s {
    uint8_t program;
    uint8_t volume;
    uint8_t expression;
    uint16_t pitch_bend;
    uint8_t sustain;
    uint8_t key [128]; /* Stores velocity, or 0 if the key is up */
    uint8_t synth_id [128]; /* Reference to (chip, channel) that is currently sounding this key */
    uint16_t rpn;

    /* RPN Parameters */
    uint8_t bend_sensitivity_semitones;
    uint8_t bend_sensitivity_cents;
} MIDI_Channel;


typedef struct MIDI_Track_s {

    /* Track state */
    uint32_t index;
    uint32_t track_end;     /* Index of the first byte outside of the current track */
    MIDI_Expect expect;     /* Next expected element in track */
    uint8_t status;         /* Status byte for running events */
    uint32_t delay;         /* Number of time intervals to delay before processing the next event */
    bool end_of_track;      /* Set to true once the track has ended */

    /* Channel state */
    MIDI_Channel channel [16];

} MIDI_Track;


typedef struct MIDI_Player_Context_s {

    uint8_t *midi;
    uint32_t midi_size;

    /* MIDI state */
    uint32_t index;
    MIDI_Track *track;
    uint32_t tick_length;   /* Number of NTSC Colourburst clocks per MIDI tick */
    uint32_t clocks;        /* Unspent NTSC Colourburst clock cycles */
    uint32_t tempo;         /* µs per quarter-note */
    uint32_t n_tracks_completed; /* Count of tracks that have received an end-of-track event */

    /* Values read from MIDI header */
    uint32_t format;
    uint32_t n_tracks;
    uint32_t tick_div;

    /* YM2413 Synth State */
    YM2413_Context *ym2413_context [MIDI_YM2413_COUNT];
    uint64_t ym2413_millicycles; /* Remaining time to carry over to the next run of the chip. */

    /* Ring-buffer queue of available YM2413 channels */
    uint8_t synth_queue [MIDI_SYNTH_QUEUE_SIZE];
    uint32_t synth_queue_get;
    uint32_t synth_queue_put;

    /* Separate queues for the five rhythm sounds */
    uint8_t rhythm_queue [5] [MIDI_RHYTHM_QUEUE_SIZE];
    uint32_t rhythm_queue_get [5];
    uint32_t rhythm_queue_put [5];

    /* Visualisation */
    uint32_t frame_clock_counter;
    Video_Frame frame_buffer;

} MIDI_Player_Context;

/* Initialize the MIDI Player */
MIDI_Player_Context *midi_player_init (void);
