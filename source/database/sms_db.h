/*
 * SMS ROM Database.
 */

#define SMS_HINT_PAL_ONLY               0x0001
#define SMS_HINT_PADDLE_ONLY            0x0002
#define SMS_HINT_SMS1_VDP               0x0004
#define SMS_HINT_LIGHT_PHASER           0x0008
#define SMS_HINT_RAM_PATTERN            0x0010

#define SMS_HINT_MAPPER_SEGA            0x0020
#define SMS_HINT_MAPPER_CODEMASTERS     0x0040
#define SMS_HINT_MAPPER_KOREAN          0x0080
#define SMS_HINT_MAPPER_MSX             0x0100
#define SMS_HINT_MAPPER_NEMESIS         0x0200
#define SMS_HINT_MAPPER_4PAK            0x0400
#define SMS_HINT_MAPPER_JANGGUN         0x0800

typedef struct SMS_DB_Entry_s {
    uint8_t hash [HASH_LENGTH];
    uint16_t hints;
} SMS_DB_Entry;


/* Get any hints for the supplied ROM hash. */
uint16_t sms_db_get_hints (uint8_t *hash);
