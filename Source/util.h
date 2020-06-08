/*
 * Common utilities.
 */


/* Bits */
#define BIT_0       (1 <<  0)
#define BIT_1       (1 <<  1)
#define BIT_2       (1 <<  2)
#define BIT_3       (1 <<  3)
#define BIT_4       (1 <<  4)
#define BIT_5       (1 <<  5)
#define BIT_6       (1 <<  6)
#define BIT_7       (1 <<  7)
#define BIT_8       (1 <<  8)
#define BIT_9       (1 <<  9)
#define BIT_10      (1 << 10)
#define BIT_11      (1 << 11)
#define BIT_12      (1 << 12)
#define BIT_13      (1 << 13)
#define BIT_14      (1 << 14)
#define BIT_15      (1 << 15)

typedef struct int32_Point_2D_s {
    int32_t x;
    int32_t y;
} int32_Point_2D;

typedef struct float_Colour_s {
    float r;
    float g;
    float b;
} float_Colour;

/* Load a rom file into a buffer. The buffer should be freed when no-longer needed. */
int32_t snepulator_load_rom (uint8_t **buffer, uint32_t *buffer_size, char *filename);

/* Take a screenshot */
void snepulator_take_screenshot (void);

/* Convert a float_Colour to greyscale. */
float_Colour to_greyscale (float_Colour c);
