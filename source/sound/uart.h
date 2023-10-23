
/* Write to an sn76489 register. */
void uart_write_sn76489 (uint8_t data);

/* Write to a ym2413 register. */
void uart_write_ym2413 (uint8_t addr, uint8_t data);

/* Open the UART for sound output. */
void uart_open (const char *path);
