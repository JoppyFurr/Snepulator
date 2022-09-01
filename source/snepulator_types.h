/*
 * Common types for Snepulator.
 */

#define HASH_LENGTH 12
#define UUID_SIZE 16

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

/* Sizes */
#define SIZE_1K     ( 1 << 10)
#define SIZE_2K     ( 2 << 10)
#define SIZE_4K     ( 4 << 10)
#define SIZE_8K     ( 8 << 10)
#define SIZE_16K    (16 << 10)
#define SIZE_32K    (32 << 10)

typedef union uint16_t_Split_u {
    uint16_t w;
    struct {
        uint8_t l;
        uint8_t h;
    };
} uint16_t_Split;

typedef struct int32_Point_2D_s {
    int32_t x;
    int32_t y;
} int32_Point_2D;

typedef struct uint_pixel_s {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} uint_pixel;
