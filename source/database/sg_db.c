/*
 * Snepulator
 * SG-1000 ROM Database.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../snepulator_types.h"
#include "../snepulator.h"
#include "sg_db.h"

SG_DB_Entry sg_db [] = {

    /* Terebi Oekaka (Japan) */
    { { 0xb4, 0x07, 0x1b, 0x78, 0x8e, 0xfb, 0x35, 0x83, 0xf8, 0x95, 0x39, 0xd4 }, SG_HINT_MAPPER_GRAPHIC_BOARD },
};


/*
 * Get any hints for the supplied ROM hash.
 */
uint16_t sg_db_get_hints (uint8_t *hash)
{

    for (uint32_t i = 0; i < (sizeof (sg_db) / sizeof (SG_DB_Entry)) ; i++)
    {
        if (memcmp (hash, sg_db [i].hash, HASH_LENGTH) == 0)
        {
            return sg_db [i].hints;
        }
    }

    /* No hints by default */
    return 0x0000;
}
