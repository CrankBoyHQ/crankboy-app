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

SCRIPT_BREAKPOINT(BANK_ADDR(0x01, 0x63E2))
{
    playdate->system->logToConsole("[SCRIPT] Copyright");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x01, 0x63E5))
{
    playdate->system->logToConsole("[SCRIPT] Call LoadFontsExtra");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x3E, 0x748A))
{
    playdate->system->logToConsole("[SCRIPT] _LoadFontsExtra1");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x3E, 0x74B0))
{
    playdate->system->logToConsole("[SCRIPT] _LoadFontsExtra2");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x3E, 0x74BD))
{
    playdate->system->logToConsole("[SCRIPT] (done) _LoadFontsExtra2");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x01, 0x63E8))
{
    playdate->system->logToConsole("[SCRIPT] (done) LoadFontsExtra");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x00, 0x0EF6))
{
    playdate->system->logToConsole("[SCRIPT] Request2bpp.wait");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x00, 0x0F0D))
{
    playdate->system->logToConsole("[SCRIPT] Request2bpp.wait2");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x00, 0x0F16))
{
    playdate->system->logToConsole("[SCRIPT] Request2bpp.wait2 (done)");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x00, 0x02B1))
{
    playdate->system->logToConsole("[SCRIPT] VBlank_Normal");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x00, 0x1769))
{
    playdate->system->logToConsole("[SCRIPT] Serve2BppRequest");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x00, 0x177D))
{
    playdate->system->logToConsole("[SCRIPT] _Serve2BppRequest");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x01, 0x63FA))
{
    playdate->system->logToConsole("[SCRIPT] Jp PlaceString");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x39, 0x45AB))
{
    playdate->system->logToConsole("[SCRIPT] FarCall Copyright");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x39, 0x45AC))
{
    playdate->system->logToConsole("[SCRIPT] Post-Copyright");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x39, 0x45BC))
{
    playdate->system->logToConsole("[SCRIPT] FarCall StopIfDMG");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x39, 0x45BD))
{
    playdate->system->logToConsole("[SCRIPT] call GameFreakPresentsInit");
}

SCRIPT_BREAKPOINT(BANK_ADDR(0x39, 0x4579))
{
    playdate->system->logToConsole("[SCRIPT] SplashScreen");
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