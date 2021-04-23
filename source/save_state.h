/*
 * Save-state support.
 */

#define SAVE_STATE_MAGIC        "SNEPSAVE"

/* Consoles */
#define CONSOLE_ID_SG_1000      "SG\0"
#define CONSOLE_ID_COLECOVISION "COL"
#define CONSOLE_ID_SMS          "SMS"
#define CONSOLE_ID_GAME_GEAR    "GG\0"

/* Console-specific state */
#define SECTION_ID_SMS_HW       "SMS"

/* Memory */
#define SECTION_ID_RAM          "RAM"
#define SECTION_ID_SRAM         "SRAM"
#define SECTION_ID_VRAM         "VRAM"

/* CPUs */
#define SECTION_ID_Z80          "Z80"

/* Sound Chips */
#define SECTION_ID_PSG          "PSG"

/* Video Chips */
#define SECTION_ID_VDP          "VDP"


/* Begin creating a new save state. */
void save_state_begin (const char *console_id);

/* Add a section to the save state. */
void save_state_section_add (const char *section_id, uint32_t version, uint32_t size, void *data);

/* Write the save state to disk. */
void save_state_write (const char *filename);

/* Load a state buffer from file. */
int load_state_begin (const char *filename, const char **console_id, uint32_t *sections_loaded);

/* Get a pointer to the next section. */
void load_state_section (const char **section_id, uint32_t *version, uint32_t *size, void **data);

/* Free the state buffer. */
void load_state_end (void);
