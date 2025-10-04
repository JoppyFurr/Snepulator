/*
 * Snepulator
 * VGM / MIDI visualiser colours.
 */

/* Colours for visualisation */
static const uint_pixel_t bright_red    = { .r = 255, .g =   0, .b =   0 };
static const uint_pixel_t dim_red       = { .r = 170, .g =   0, .b =   0 };
static const uint_pixel_t bright_yellow = { .r = 255, .g = 255, .b =   0 };
static const uint_pixel_t dim_yellow    = { .r = 170, .g = 170, .b =   0 };
static const uint_pixel_t bright_green  = { .r =   0, .g = 255, .b =   0 };
static const uint_pixel_t dim_green     = { .r =   0, .g = 170, .b =   0 };
static const uint_pixel_t white         = { .r = 255, .g = 255, .b = 255 };
static const uint_pixel_t light_grey    = { .r = 170, .g = 170, .b = 170 };
static const uint_pixel_t dark_grey     = { .r =  85, .g =  85, .b =  85 };

/* Colour groupings */
static const uint_pixel_t colours_peak [15] = { bright_green, bright_green,  bright_green,  bright_green, bright_green,
                                                bright_green, bright_green,  bright_green,  bright_green, bright_green,
                                                bright_green, bright_yellow, bright_yellow, bright_red,   bright_red };

static const uint_pixel_t colours_base [15] = { dim_green, dim_green,  dim_green,  dim_green, dim_green,
                                                dim_green, dim_green,  dim_green,  dim_green, dim_green,
                                                dim_green, dim_yellow, dim_yellow, dim_red,   dim_red };
