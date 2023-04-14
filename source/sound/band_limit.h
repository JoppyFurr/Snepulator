/*
 * Band-limiting.
 */

#define DIFF_RING_SIZE 48

typedef struct Bandlimit_Context_s {
    double diff_ring [DIFF_RING_SIZE];
    uint32_t diff_ring_index;
    int16_t previous_input;
    double output;
} Bandlimit_Context;

/* Apply band-limited synthesis to non-limited input. */
void band_limit_samples (Bandlimit_Context *context, int16_t *sample, int16_t *phase, int count);

/* Initialize data for band limited synthesis. */
Bandlimit_Context *band_limit_init (void);
