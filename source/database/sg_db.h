/*
 * Snepulator
 * SG-1000 ROM Database.
 */

#define SG_HINT_MAPPER_GRAPHIC_BOARD    0x0200
#define SG_HINT_MAPPER_EXTRA_RAM        0x0400
#define SG_HINT_MAPPER_DAHJEE_RAM       0x0800

typedef struct SG_DB_Entry_s {
    uint8_t hash [HASH_LENGTH];
    uint16_t hints;
} SG_DB_Entry;


/* Get any hints for the supplied ROM hash. */
uint16_t sg_db_get_hints (uint8_t *hash);
