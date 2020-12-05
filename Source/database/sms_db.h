/*
 * SMS ROM Database.
 */

#define SMS_HINT_PAL_ONLY       0x01
#define SMS_HINT_PADDLE_ONLY    0x02
#define SMS_HINT_SMS1_VDP       0x04
#define SMS_HINT_LIGHT_PHASER   0x08

typedef struct SMS_DB_Entry_s {
    uint8_t hash [HASH_LENGTH];
    uint8_t hints;
} SMS_DB_Entry;


/* Get any hints for the supplied ROM hash. */
uint8_t sms_db_get_hints (uint8_t *hash);
