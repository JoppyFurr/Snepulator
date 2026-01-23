/*
 * Snepulator
 * Types and constants
 */

#define HASH_LENGTH 12
#define UUID_SIZE 16

/* Extra mathematical constants */
#ifndef M_PI
#define M_PI    (3.14159265358979323846)
#endif
#define M_PHI   (1.61803398874989484820)

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
#define SIZE_128    128
#define SIZE_1K     ( 1 << 10)
#define SIZE_2K     ( 2 << 10)
#define SIZE_4K     ( 4 << 10)
#define SIZE_8K     ( 8 << 10)
#define SIZE_16K    (16 << 10)
#define SIZE_32K    (32 << 10)
#define SIZE_48K    (48 << 10)
#define SIZE_64K    (64 << 10)


typedef union uint16_split_u {
    uint16_t w;
    struct {
        uint8_t l;
        uint8_t h;
    };
} uint16_split_t;


typedef union uint32_split_u {
    uint32_t l;
    struct {
        uint16_t w_low;
        uint16_t w_high;
    };
    uint16_t w;
    uint8_t b;
} uint32_split_t;


typedef struct int_point_s {
    int32_t x;
    int32_t y;
} int_point_t;


typedef struct double_point_s {
    double x;
    double y;
    double z;
} double_point_t;


typedef struct uint_pixel_s {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} uint_pixel_t;
