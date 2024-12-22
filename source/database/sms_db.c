/*
 * Snepulator
 * SMS ROM Database.
 *
 * PAL-Only list comes from SMS Power.
 */

#include <string.h>

#include "../snepulator.h"
#include "sms_db.h"

SMS_DB_Entry sms_db [] = {

    /* 4 PAK All Action (Australia) */
    { { 0x58, 0x6d, 0xb8, 0x6d, 0xdf, 0x03, 0xcd, 0x3e, 0x21, 0x87, 0xc0, 0x29 }, SMS_HINT_MAPPER_4PAK },

    /* 94 Super World Cup Soccer (Korea) */
    { { 0xb2, 0x3a, 0x98, 0xb2, 0xcf, 0x55, 0x8c, 0x2b, 0x28, 0xfe, 0x97, 0x23 }, SMS_HINT_MAPPER_KOREAN },

    /* The Adams Family */
    { { 0x4b, 0xe4, 0x54, 0xc3, 0xd8, 0xec, 0x0e, 0x00, 0x37, 0xe3, 0x77, 0x2d }, SMS_HINT_PAL_ONLY },

    /* Alex Kidd BMX Trial */
    { { 0x3a, 0xfb, 0xfd, 0xc1, 0x15, 0x41, 0x07, 0x36, 0x1a, 0x24, 0xdc, 0x74 }, SMS_HINT_PADDLE_ONLY },

    /* Alibaba and 40 Thieves (Korea) */
    { { 0x3a, 0x8a, 0x07, 0x38, 0xd7, 0x07, 0x9e, 0xeb, 0xdd, 0xd9, 0xeb, 0xbb }, SMS_HINT_RAM_PATTERN },

    /* Assault City (Light Phaser) */
    { { 0x2e, 0x38, 0xb6, 0xe0, 0xb1, 0x48, 0x16, 0x66, 0x58, 0x3d, 0xb6, 0xea }, SMS_HINT_LIGHT_PHASER },

    /* Back to the Future II */
    { { 0xb0, 0xfb, 0xd1, 0xbc, 0xd0, 0xc3, 0x54, 0x7e, 0x2a, 0x9b, 0xa8, 0x5d }, SMS_HINT_PAL_ONLY },

    /* Back to the Future III */
    { { 0xa2, 0xab, 0x97, 0xd8, 0x0c, 0xc3, 0x0a, 0x4f, 0x92, 0xf1, 0x57, 0x9c }, SMS_HINT_PAL_ONLY },

    /* Bart vs. The Space Mutants */
    { { 0xb2, 0x51, 0x35, 0x66, 0xdb, 0x41, 0xe6, 0xfa, 0xc8, 0xb8, 0xf4, 0x55 }, SMS_HINT_PAL_ONLY },

    /* Block Hole (Korea) */
    { { 0x59, 0xae, 0x01, 0x94, 0xb9, 0x0b, 0x55, 0x64, 0x7a, 0x6b, 0x55, 0x37 }, SMS_HINT_RAM_PATTERN },

    /* Bobble Bobble (Korea) */
    { { 0xfe, 0x90, 0xf9, 0x1d, 0xa5, 0x15, 0x56, 0xaf, 0xb6, 0x1e, 0xf7, 0x53 }, SMS_HINT_MAPPER_NONE },

    /* C_So! (Korea) */
    { { 0x60, 0x31, 0x3c, 0x6c, 0xd3, 0xdd, 0xd4, 0x8c, 0x2d, 0xd3, 0x1b, 0x0f }, SMS_HINT_MAPPER_NONE },

    /* California Games II (Europe) */
    { { 0x31, 0x9f, 0x17, 0x11, 0xb7, 0x3a, 0x84, 0x07, 0x54, 0xe2, 0xd2, 0x26 }, SMS_HINT_PAL_ONLY },

    /* Champions of Europe (Europe) */
    { { 0x2e, 0xf0, 0xfb, 0x8e, 0x95, 0xc6, 0xac, 0x84, 0x3c, 0xd3, 0xc9, 0xb2 }, SMS_HINT_MAPPER_SEGA },

    /* Chase H.Q. */
    { { 0xc4, 0xd1, 0x6a, 0xb6, 0x14, 0xd0, 0x79, 0xb0, 0x74, 0x91, 0xdf, 0xdd }, SMS_HINT_PAL_ONLY },

    /* Cosmic Spacehead */
    { { 0x2b, 0x10, 0x37, 0x73, 0x7b, 0xa8, 0x4a, 0x46, 0x86, 0xf2, 0x07, 0xb2 }, SMS_HINT_PAL_ONLY },

    /* Desert Strike */
    { { 0xcb, 0x99, 0x91, 0xcb, 0x97, 0xb5, 0xfb, 0xb7, 0xbc, 0xd3, 0x86, 0xd9 }, SMS_HINT_PAL_ONLY },

    /* The Excellent Dizzy Collection */
    { { 0xf0, 0x12, 0x04, 0xdc, 0xc5, 0xe1, 0x29, 0xec, 0x37, 0xc4, 0x83, 0xb9 }, SMS_HINT_PAL_ONLY },

    /* FA Tetris (Korea) */
    { { 0x5c, 0x44, 0xde, 0xcf, 0x6e, 0x78, 0x2c, 0xf2, 0x41, 0xb7, 0xaf, 0x17 }, SMS_HINT_MAPPER_NONE },

    /* Flashpoint (Korea) */
    { { 0x09, 0xd5, 0xc4, 0x11, 0xf9, 0x00, 0x34, 0x10, 0xaf, 0x7e, 0xff, 0x74 }, SMS_HINT_MAPPER_NONE },

    /* Fantastic Dizzy */
    { { 0x02, 0x81, 0xd8, 0x15, 0xbd, 0xb9, 0x7a, 0xd6, 0x7a, 0xc8, 0x14, 0x57 }, SMS_HINT_PAL_ONLY },

    /* Galactic Protector */
    { { 0x7d, 0xca, 0x21, 0xc0, 0xcc, 0xda, 0x24, 0xa0, 0xf7, 0x4d, 0x28, 0xcb }, SMS_HINT_PADDLE_ONLY },

    /* Gangster Town */
    { { 0xc7, 0x08, 0x21, 0xb0, 0x03, 0x38, 0x5d, 0x0f, 0x33, 0xa3, 0x7a, 0xb4 }, SMS_HINT_LIGHT_PHASER },

    /* Home Alone */
    { { 0x65, 0x8c, 0x10, 0x14, 0x66, 0x16, 0x7e, 0xa4, 0x33, 0x0f, 0x3f, 0x1c }, SMS_HINT_PAL_ONLY },

    /* James Bond 007: The Duel (Europe) */
    { { 0x46, 0x9e, 0xfd, 0xc8, 0xd4, 0x2b, 0x9d, 0x2c, 0xae, 0xa5, 0x55, 0x2e }, SMS_HINT_PAL_ONLY },

    /* Jang Pung 3 (Korea) */
    { { 0x0c, 0x98, 0x86, 0xa0, 0x86, 0xc7, 0x95, 0x34, 0x59, 0x77, 0xd0, 0xb9 }, SMS_HINT_MAPPER_KOREAN },

    /* Janggun-ue Adeul (Korea) */
    { { 0x73, 0xef, 0xbf, 0x80, 0xc1, 0x92, 0x45, 0xb0, 0xed, 0x15, 0xe3, 0x04 }, SMS_HINT_MAPPER_JANGGUN },

    /* Laser Ghost */
    { { 0x47, 0x71, 0xdf, 0x2f, 0xb5, 0x9d, 0x4a, 0x7b, 0x3d, 0x15, 0xdf, 0x26 }, SMS_HINT_PAL_ONLY },

    /* Marksman Shooting + Trap Shooting */
    { { 0xff, 0x5d, 0x2a, 0xff, 0xbb, 0xe8, 0xf0, 0x1a, 0xb3, 0x77, 0x7b, 0xc1 }, SMS_HINT_LIGHT_PHASER },

    /* Marksman Shooting + Trap Shooting + Safari Hunt */
    { { 0x34, 0xea, 0x9b, 0x2d, 0x75, 0x30, 0x97, 0x5e, 0x20, 0xa8, 0xc2, 0x5d }, SMS_HINT_LIGHT_PHASER },

    /* Megumi Rescue */
    { { 0xf6, 0x51, 0x54, 0x60, 0x5f, 0x95, 0x44, 0xfc, 0xae, 0x56, 0xdd, 0x43 }, SMS_HINT_PADDLE_ONLY },

    /* Micro Machines */
    { { 0x3d, 0x18, 0xce, 0x40, 0xe5, 0x77, 0x3d, 0x4d, 0x01, 0xae, 0x0a, 0xb7 }, SMS_HINT_PAL_ONLY },

    /* Missile Defense 3D */
    { { 0xc4, 0x34, 0x87, 0x4d, 0x2d, 0x26, 0x7f, 0x7c, 0x6b, 0x76, 0x11, 0xec }, SMS_HINT_LIGHT_PHASER },

    /* MSX Soccer (Korea) */
    { { 0x74, 0xb6, 0x9f, 0x44, 0x93, 0x6b, 0xfa, 0xb1, 0xcc, 0x02, 0xaf, 0x20 }, SMS_HINT_MAPPER_NONE },

    /* Nemesis (Korea) */
    { { 0x83, 0xcc, 0x8d, 0xe2, 0x2f, 0xac, 0xb3, 0x52, 0x1e, 0x50, 0x36, 0xea }, SMS_HINT_MAPPER_NEMESIS },

    /* The New Zealand Story */
    { { 0x65, 0x79, 0xba, 0x1d, 0xe6, 0xc0, 0x77, 0xdb, 0x26, 0xa9, 0xc7, 0x1c }, SMS_HINT_PAL_ONLY },

    /* Operation Wolf */
    { { 0x8e, 0xd3, 0xe0, 0x72, 0xc7, 0x7d, 0xb5, 0xa1, 0xdb, 0x3c, 0x3e, 0x54 }, SMS_HINT_PAL_ONLY | SMS_HINT_LIGHT_PHASER },

    /* Pooyan (Korea) */
    { { 0xab, 0x87, 0xd5, 0x1c, 0x48, 0x7d, 0xd8, 0x15, 0x34, 0x8d, 0xaa, 0xcf }, SMS_HINT_MAPPER_NONE },

    /* Predator 2 (Europe) */
    { { 0xe5, 0x3f, 0x5d, 0xcd, 0x3e, 0xc4, 0xa2, 0xce, 0x64, 0xe0, 0x9d, 0xd0 }, SMS_HINT_PAL_ONLY },

    /* Power Boggle Boggle (Korea) */
    { { 0x99, 0xa7, 0x9e, 0x7e, 0xbe, 0x5b, 0x6d, 0x09, 0xce, 0x78, 0x0a, 0xb5 }, SMS_HINT_MAPPER_NONE | SMS_HINT_NO_MEMORY_CONTROL },

    /* Rambo III */
    { { 0xbf, 0x9b, 0x83, 0x4c, 0x99, 0xee, 0xfd, 0x48, 0xd0, 0xd0, 0x52, 0xef }, SMS_HINT_LIGHT_PHASER },

    /* Rescue Mission */
    { { 0x89, 0x15, 0xd7, 0x79, 0x36, 0x35, 0xc7, 0x37, 0xe2, 0xdd, 0xde, 0xa2 }, SMS_HINT_LIGHT_PHASER },

    /* RoboCop 3 */
    { { 0x44, 0x8a, 0x32, 0x63, 0xf1, 0x8a, 0xe2, 0x80, 0x8a, 0x84, 0xbf, 0x3d }, SMS_HINT_PAL_ONLY },

    /* Sangokushi 3 (Korea) */
    { { 0x93, 0x4f, 0xc7, 0x19, 0x8d, 0xb1, 0xee, 0x92, 0xe5, 0x91, 0xf3, 0xe5 }, SMS_HINT_MAPPER_KOREAN },

    /* Sensible Soccer */
    { { 0x57, 0x9d, 0xf5, 0xf1, 0xae, 0xbc, 0x40, 0x29, 0xda, 0x0f, 0x24, 0x2b }, SMS_HINT_PAL_ONLY },

    /* Shadow of the Beast */
    { { 0xa1, 0xd1, 0x02, 0x09, 0x78, 0xae, 0x99, 0x83, 0x52, 0xba, 0x96, 0x45 }, SMS_HINT_PAL_ONLY },

    /* Shooting Gallery */
    { { 0xf6, 0x27, 0x74, 0x74, 0x4c, 0xce, 0x67, 0x90, 0xf3, 0x95, 0xaf, 0x21 }, SMS_HINT_LIGHT_PHASER },

    /* Sonic the Hedgehog 2 */
    { { 0xc0, 0xd6, 0x67, 0xe7, 0xa2, 0x06, 0x71, 0x79, 0x3e, 0x72, 0x48, 0x07 }, SMS_HINT_PAL_ONLY },
    { { 0x84, 0xf3, 0x8d, 0x00, 0xc1, 0x88, 0x42, 0x00, 0x98, 0xfc, 0xe2, 0x8e }, SMS_HINT_PAL_ONLY },

    /* Space Gun */
    { { 0x82, 0xaf, 0x7a, 0x77, 0xc7, 0xc3, 0xb2, 0x15, 0xd6, 0x95, 0x40, 0x0d }, SMS_HINT_LIGHT_PHASER },

    /* Space Harrier (Europe) */
    { { 0xb3, 0x57, 0x1a, 0x74, 0x6c, 0x8c, 0x20, 0xc4, 0xf2, 0xa3, 0x3f, 0x88 }, SMS_HINT_PAL_ONLY },

    /* Suho Cheonsa (Korea) */
    { { 0x65, 0x80, 0x93, 0x8c, 0xe3, 0x9d, 0x97, 0x6c, 0x36, 0xa2, 0x67, 0x41 }, SMS_HINT_MAPPER_KOREAN },

    /* The Three Dragon Story (Korea) */
    { { 0xe7, 0x6c, 0x8c, 0x03, 0x8c, 0x3b, 0x10, 0xba, 0x59, 0x57, 0xa0, 0xc2 }, SMS_HINT_MAPPER_NONE },

    /* Wanted */
    { { 0xff, 0xf9, 0x82, 0x7a, 0x8b, 0x3b, 0x48, 0xb6, 0x20, 0xc1, 0xca, 0x7f }, SMS_HINT_LIGHT_PHASER },

    /* Woody Pop */
    { { 0xdb, 0x46, 0x7e, 0xa4, 0x78, 0x15, 0x89, 0x7d, 0x2f, 0xa9, 0xa4, 0xb4 }, SMS_HINT_PADDLE_ONLY },

    /* Xenon 2 */
    { { 0xca, 0xdc, 0xd2, 0x6a, 0x06, 0x68, 0x58, 0x1c, 0x1d, 0x67, 0xaa, 0xee }, SMS_HINT_PAL_ONLY },
    { { 0xc3, 0x26, 0x9e, 0xf5, 0x33, 0xb4, 0x20, 0xa1, 0x3a, 0x44, 0x80, 0xf0 }, SMS_HINT_PAL_ONLY },

    /* Xyzolog (Korea) */
    { { 0x9e, 0x17, 0x50, 0xef, 0x93, 0xde, 0xb7, 0xbe, 0xaa, 0x89, 0x1e, 0xd6 }, SMS_HINT_MAPPER_NONE },

    /* Ys (Japan) */
    { { 0x8a, 0xf0, 0x9c, 0x9c, 0xb2, 0x12, 0x9d, 0x21, 0x27, 0xdb, 0xd6, 0xe4 }, SMS_HINT_SMS1_VDP },
};




/*
 * Get any hints for the supplied ROM hash.
 */
uint16_t sms_db_get_hints (uint8_t *hash)
{

    for (uint32_t i = 0; i < (sizeof (sms_db) / sizeof (SMS_DB_Entry)) ; i++)
    {
        if (memcmp (hash, sms_db [i].hash, HASH_LENGTH) == 0)
        {
            return sms_db [i].hints;
        }
    }

    /* No hints by default */
    return 0x0000;
}
