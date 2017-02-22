
void z80_reset ();

uint32_t z80_run (uint8_t (* memory_read) (uint16_t),
              void    (* memory_write)(uint16_t, uint8_t),
              uint8_t (* io_read)     (uint16_t),
              void    (* io_write)    (uint16_t, uint8_t));
