/*
 * SMS ROM Database.
 */

#define SMS_HINT_PAL_ONLY       0x01
#define SMS_HINT_PADDLE_ONLY    0x02

typedef struct SMS_DB_Entry_s {
    uint8_t hash [HASH_LENGTH];
    uint8_t hints;
} SMS_DB_Entry;


/* Get any hints for the supplied ROM hash. */
uint8_t sms_db_get_hints (uint8_t *hash);
