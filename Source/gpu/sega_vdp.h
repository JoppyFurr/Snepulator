
/* Enums */
typedef enum Vdp_Port_t {
    VDP_PORT_V_COUNTER,
    VDP_PORT_H_COUNTER,
    VDP_PORT_DATA,
    VDP_PORT_CONTROL
} Vdp_Port;

typedef enum Vdp_Operation_t {
    VDP_OPERATION_WRITE,
    VDP_OPERATION_READ,
} Vdp_Operation;

typedef enum Vdp_Code_t {
    VDP_CODE_VRAM_READ  = 0x00,
    VDP_CODE_VRAM_WRITE = 0x01,
    VDP_CODE_REG_WRITE  = 0x02,
    VDP_CODE_CRAM_WRITE = 0x03,
} Vdp_Code;


/* Functions */
void vdp_init (void);
void vdp_dump (void);
void vdp_render (void);
uint32_t vdp_access (uint8_t value, Vdp_Port port, Vdp_Operation operation);
