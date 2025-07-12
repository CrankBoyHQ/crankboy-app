#pragma once

// convenient import for scripts

#include "app.h"
#include "game_scene.h"
#include "peanut_gb/peanut_gb.h"
#include "preferences.h"
#include "script.h"
#include "utility.h"
#include "game_scene.h"

typedef unsigned romaddr_t;
typedef u16 addr16_t;

extern struct gb_s* script_gb;

u8 rom_peek(romaddr_t addr);
void rom_poke(romaddr_t addr, u8 v);

u8 ram_peek(addr16_t addr);
void ram_poke(addr16_t addr, u8 v);

romaddr_t rom_size(void);

#define force_pref(pref, val)                                                                      \
    do                                                                                             \
    {                                                                                              \
        preferences_##pref = val;                                                                  \
        prefs_locked_by_script |= PREFBIT_##pref; \
    } while (0)

void poke_verify(unsigned bank, u16 addr, u8 prev, u8 val);

// bank: can be -1, in which case search the whole ROM (still bank-aligned though.)
void find_code_cave(int bank, romaddr_t* o_max_start, romaddr_t* o_max_size);

#define script_error(...) playdate->system->error(__VA_ARGS__)

#define $A (script_gb->cpu_reg.a)
#define $F (script_gb->cpu_reg.f)
#define $Fc (script_gb->cpu_reg.f_bits.c)
#define $Fh (script_gb->cpu_reg.f_bits.h)
#define $Fn (script_gb->cpu_reg.f_bits.n)
#define $Fz (script_gb->cpu_reg.f_bits.z)
#define $AF (script_gb->cpu_reg.af)
#define $B (script_gb->cpu_reg.b)
#define $C (script_gb->cpu_reg.c)
#define $BC (script_gb->cpu_reg.bc)
#define $D (script_gb->cpu_reg.d)
#define $E (script_gb->cpu_reg.e)
#define $DE (script_gb->cpu_reg.de)
#define $H (script_gb->cpu_reg.h)
#define $L (script_gb->cpu_reg.l)
#define $HL (script_gb->cpu_reg.hl)
#define $PC (script_gb->cpu_reg.pc)
#define $SP (script_gb->cpu_reg.sp)

#define PAD_A 1
#define PAD_B 2
#define PAD_SELECT 4
#define PAD_START 8
#define PAD_RIGHT 0x10
#define PAD_LEFT 0x20
#define PAD_UP 0x40
#define PAD_DOWN 0x80

#define OP_NOP 0x00
#define OP_LD_B_d8 0x06
#define OP_RLCA 0x07
#define OP_ADD_HL_BC 0x09
#define OP_RRCA 0x0F
#define OP_JR 0x18
#define OP_JR_nz 0x20
#define OP_LD_HL_d16 0x21
#define OP_INC_HL 0x23
#define OP_LD_H_d8 0x26
#define OP_JR_z 0x28
#define OP_LD_A_iHL 0x2A
#define OP_DEC_HL 0x2B
#define OP_JR_nc 0x30
#define OP_JR_ge 0x30
#define OP_SCF 0x37
#define OP_JR_c 0x38
#define OP_JR_lt 0x38
#define OP_LD_A_d8 0x3E
#define OP_CCF 0x3F
#define OP_LD_B_H 0x44
#define OP_LD_B_A 0x47
#define OP_LD_C_A 0x4F
#define OP_LD_C_L 0x4D
#define OP_LD_H_xHL 0x66
#define OP_LD_H_A 0x67
#define OP_LD_L_A 0x6F
#define OP_LD_A_B 0x78
#define OP_LD_A_C 0x79
#define OP_LD_A_H 0x7C
#define OP_LD_A_L 0x7D
#define OP_SUB_L 0x95
#define OP_SBC_H 0x9C
#define OP_AND_xHL 0xA6
#define OP_AND_A 0xA7
#define OP_XOR_A 0xAF
#define OP_POP_BC 0xC1
#define OP_JP 0xC3
#define OP_PUSH_BC 0xC5
#define OP_OR_B 0xB0
#define OP_OR_A 0xB7
#define OP_CP_xHL 0xBE
#define OP_CALL_nz 0xC4
#define OP_RET 0xC9
#define OP_CALL 0xCD
#define OP_SUB_d8 0xD6
#define OP_RET_c 0xD8
#define OP_CALL_c 0xDC
#define OP_POP_HL 0xE1
#define OP_PUSH_HL 0xE5
#define OP_AND_d8 0xE6
#define OP_LD_a16_A 0xEA
#define OP_XOR_d8 0xEE
#define OP_POP_AF 0xF1
#define OP_PUSH_AF 0xF5
#define OP_OR_d8 0xF6
#define OP_LD_A_a16 0xFA
#define OP_CP_d8 0xFE

#define OP_RRC_B 0xCB08
#define OP_RRC_C 0xCB09
#define OP_BIT0_A 0xCB47
#define OP_BIT0_H 0xCB44
#define OP_BIT3_A 0xCB5F
#define OP_BIT6_A 0xCB77
#define OP_BIT7_H 0xCB7C
#define OP_RR_L 0xCB1D
#define OP_SRA_H 0xCB2C
#define OP_SRL_H 0xCB3C
#define OP_RR_C 0xCB19
#define OP_SRA_B 0xCB28
#define OP_SRL_B 0xCB38
#define OP_SWAP_A 0xCB37
#define OP_SRL_A 0xCB3F

#define IO_PD_FEATURE_SET 0xFF57
#define IO_PD_CRANK_DOCKED 0xFF57
#define IO_PD_CRANK_lo 0xFF58
#define IO_PD_CRANK_hi 0xFF59

// TODO
#define K_BUTTON_A 0x01
#define K_BUTTON_B 0x02
#define K_BUTTON_SELECT 0x04
#define K_BUTTON_START 0x08
#define K_BUTTON_RIGHT 0x10
#define K_BUTTON_LEFT 0x20
#define K_BUTTON_UP 0x40
#define K_BUTTON_DOWN 0x80

struct CScriptNode
{
    const struct CScriptInfo* info;
    struct CScriptNode* next;
};

extern struct CScriptNode* c_script_list_head;

#define C_SCRIPT__(x)                                                         \
    static struct CScriptInfo _script_info_##x;                               \
    static struct CScriptNode _script_node_##x = {.info = &_script_info_##x}; \
    static __attribute__((constructor)) void _init_script_##x(void)           \
    {                                                                         \
        _script_node_##x.next = c_script_list_head;                           \
        c_script_list_head = &_script_node_##x;                               \
    }                                                                         \
    static struct CScriptInfo _script_info_##x =
#define C_SCRIPT_(x) C_SCRIPT__(x)
#define C_SCRIPT C_SCRIPT_(__COUNTER__)

static struct ScriptBreakpointDef
{
    CS_OnBreakpoint bp;
    romaddr_t* rom_addrs;
    struct ScriptBreakpointDef* next;
}* script_breakpoints = NULL;

/*
    breakpoint usage:

    #define USERDATA MyUserdataType* ud

    SCRIPT_BREAKPOINT(
        // different addresses for different configurations
        {addr0, addr1, addr2, ...}
    ) {
        // function body; accessible args are
        // gb, addr, bpidx, ud
    }

    // in on_begin:

    SET_BREAKPOINTS(configuration number);

    // alternatively, just directly use `c_script_add_hw_breakpoint(gb, addr, cb)`
*/

#define SCRIPT_BREAKPOINT__(x, ...)                                             \
    static void breakpoint_##x(struct gb_s* gb, u16 addr, int bpidx, USERDATA); \
    static romaddr_t bp_addrs_##x[] = {__VA_ARGS__};                            \
    static struct ScriptBreakpointDef script_bp_##x = {                         \
        .bp = (CS_OnBreakpoint)breakpoint_##x,                                  \
        .rom_addrs = bp_addrs_##x,                                              \
    };                                                                          \
    static __attribute__((constructor)) void setup_bp_##x(void)                 \
    {                                                                           \
        script_bp_##x.next = script_breakpoints;                                \
        script_breakpoints = &script_bp_##x;                                    \
    }                                                                           \
    static void breakpoint_##x(struct gb_s* gb, u16 addr, int bpidx, USERDATA)

#define SCRIPT_BREAKPOINT_(x, ...) SCRIPT_BREAKPOINT__(x, __VA_ARGS__)
#define SCRIPT_BREAKPOINT(...) SCRIPT_BREAKPOINT_(__COUNTER__, __VA_ARGS__)

#define SET_BREAKPOINTS(CONF)                                                            \
    do                                                                                   \
    {                                                                                    \
        unsigned configuration = CONF;                                                   \
        for (struct ScriptBreakpointDef* def = script_breakpoints; def; def = def->next) \
        {                                                                                \
            if (def->rom_addrs[configuration] != (romaddr_t) - 1)                        \
                c_script_add_hw_breakpoint(                                              \
                    script_gb, def->rom_addrs[configuration], (CS_OnBreakpoint)def->bp   \
                );                                                                       \
        }                                                                                \
    } while (0)

// rom address given bank and ram address
#define BANK_ADDR(bank, addr) ((bank * 0x4000) | (addr % 0x4000))

typedef struct
{
    uint8_t bank;
    uint32_t addr;
    bool unsafe;
    uint8_t* tprev;
    uint8_t* tval;
    size_t length;
    bool applied;
} CodeReplacement;

CodeReplacement* code_replacement_new(
    unsigned bank, uint16_t addr, const uint8_t* tprev, const uint8_t* tval, size_t length,
    bool unsafe
);

#define STRIP_PARENS(...) __VA_ARGS__

#define code_replacement__(x, bank, addr, tprev, tval, unsafe)                        \
    ({                                                                                \
        const u8 _tprev_##x[] = {STRIP_PARENS tprev};                                 \
        const u8 _tval_##x[] = {STRIP_PARENS tval};                                   \
        code_replacement_new(                                                         \
            bank, addr, _tprev_##x, _tval_##x, PEANUT_GB_ARRAYSIZE(_tval_##x), unsafe \
        );                                                                            \
    })
#define code_replacement_(x, bank, addr, tprev, tval, unsafe) \
    code_replacement__(x, bank, addr, tprev, tval, unsafe)
#define code_replacement(bank, addr, tprev, tval, unsafe) \
    code_replacement_(__COUNTER__, bank, addr, tprev, tval, unsafe)

void code_replacement_apply(CodeReplacement* r, bool apply);

void code_replacement_free(CodeReplacement* r);

#define $JOYPAD (script_gb->direct.joypad ^ 0xFF)

#define SCRIPT_ASSETS_DIR "images/script-assets/"