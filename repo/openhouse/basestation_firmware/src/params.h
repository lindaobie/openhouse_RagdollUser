/* Configuration parameters, layout-compatible with stock EEPROM partitions. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "params_table.h"

typedef enum {
    PT_BOOL, PT_U8, PT_U16, PT_U32, PT_I8, PT_I16, PT_I32, PT_FLOAT,
    PT_STRING, PT_RESERVED, PT_ENUM,
} ptype_t;

#define PF_RUNTIME  0x01    /* not stored in EEPROM */
#define PF_FACTORY  0x02    /* factory calibration */

/* timebase.mode enum (stock table @0xE5CC) */
enum { TB_MASTER_TDM, TB_MASTER_SYNC, TB_MASTER_ASYNC, TB_SLAVE_CABLE, TB_SLAVE_OPTICAL };
/* mode.N.ootx enum (stock table @0xE580) */
enum { OOTX_SHORT, OOTX_MEDIUM, OOTX_LONG };

#define FIELD_ctype_bool    bool
#define P_FIELD(field, name, pt, ctype, size, flags, dflt, en, desc) \
    P_FIELD_##pt(field, ctype, size)
#define P_FIELD_PT_BOOL(f, c, s)     bool f;
#define P_FIELD_PT_U8(f, c, s)       uint8_t f;
#define P_FIELD_PT_U16(f, c, s)      uint16_t f;
#define P_FIELD_PT_U32(f, c, s)      uint32_t f;
#define P_FIELD_PT_I8(f, c, s)       int8_t f;
#define P_FIELD_PT_I16(f, c, s)      int16_t f;
#define P_FIELD_PT_I32(f, c, s)      int32_t f;
#define P_FIELD_PT_FLOAT(f, c, s)    float f;
#define P_FIELD_PT_ENUM(f, c, s)     uint8_t f;
#define P_FIELD_PT_STRING(f, c, s)   char f[s];
#define P_FIELD_PT_RESERVED(f, c, s) uint8_t f[s];

typedef struct {
    PARAM_LIST(P_FIELD)
} params_t;

extern params_t P;      /* live configuration, e.g. P.mode_current, P.rotor_0_pwm */

typedef struct {
    const char *name;
    void       *ptr;
    uint8_t     type;
    uint8_t     size;
    uint8_t     flags;
} pdesc_t;

extern const pdesc_t param_desc[];
extern const unsigned param_count;

/* Result codes follow stock cparam (error strings @0x139DE). */
typedef enum {
    CP_OK = 0,
    CP_EEPROM_READ = 6, CP_EEPROM_WRITE = 7,
    CP_LOADED_DEFAULTS = 8,     /* EEPROM empty: defaults loaded and saved */
    CP_LOADED_FACTORY = 9,      /* no valid user partition: factory loaded */
    CP_NO_PARTITIONS = 10,      /* nothing valid: defaults, unsaved */
    CP_BAD_PARTITION = 11,
} cp_result_t;

void        params_defaults(void);
cp_result_t params_load(int *partition_out);
cp_result_t params_save(int *partition_out);
unsigned    params_payload_size(void);
