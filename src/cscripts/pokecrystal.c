#include "../scriptutil.h"

#define DESCRIPTION                                                              \
    "- Minor performance optimizations."

// custom data for script.
typedef struct ScriptData
{
    
} ScriptData;

// this define is used by SCRIPT_BREAKPOINT
#define USERDATA ScriptData* data

SCRIPT_BREAKPOINT(BANK_ADDR(0, 0x3041))
{
    if ($BC == 0) return;
    
    // wrong ROM
    if (ram_peek(0x3041) != 0x4 || ram_peek(0x3042) != 0xC) return;
    
    playdate->system->logToConsole("[SCRIPT] memset: len=0x%04x, 0x%04x <- 0x%02x", $BC, $HL, $A);
    
    uint8_t a = $A;
    
    while ($BC --> 0)
    {
        ram_poke($HL++, a);
    }
    
    $BC = 0;
    $PC = 0x304C;
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x00, 0x3026))
{
    if ($BC == 0) return;
    
    // wrong ROM
    if (ram_peek(0x3026) != 0x4 || ram_peek(0x3027) != 0xC) return;
    
    playdate->system->logToConsole("[SCRIPT] memcpy");
    
    while ($BC --> 0)
    {
        ram_poke($DE++, ram_peek($HL++));
    }
    
    $BC = 0;
    $PC = 0x3033;
}

static ScriptData* on_begin(gb_s* gb, char* header_name)
{
    ScriptData* data = allocz(ScriptData);
    
    SET_BREAKPOINTS(0);
    
    return data;
}

static void on_end(gb_s* gb, ScriptData* data)
{
    cb_free(data);
}

C_SCRIPT{
    .rom_name = "PM_CRYSTAL",
    .description = DESCRIPTION,
    .experimental = false,
    .on_begin = (CS_OnBegin)on_begin,
    .on_end = (CS_OnEnd)on_end,
};