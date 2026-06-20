ENTRY(_entrypoint_)
GROUP(libgcc.a libc.a libm.a)

/* Forces a particular cache-line alignment, for stability
	(reduce impact of "performance lottery")

	The particular values used were just ones that seem to
	give good performance, but further tinkering may be desirable.

	. = ALIGN(32);
	. += (OFFSET);

	The emulator's performance seems very sensitive to the location of
	some functions, so we specify their alignment precisely simply for
	stability.

	The performance lottery is also influenced by branch prediction.
	Branch prediction seems to be based on bits modulo 0x1000, so this
	provides extra stability:

	. = ALIGN(0x1000);

	--- ITCM_STEP_PAD tuning ---
	__gb_step_cpu is the hottest function. Its alignment (addr % 0x1000)
	dramatically affects FPS due to branch predictor behavior. Changing
	ANY code in .text.tick, .text.cb, or earlier ITCM subsections shifts
	its position - can gain or lose 5+ FPS on DMG games like Road Rash.

	The .step subsection (ITCM_STEP_PAD_DMG / ITCM_STEP_PAD_CGB) locks
	__gb_step_cpu at a known-good offset. Default: 0x40 (tuned for Road
	Rash on Playdate Rev A).

	To re-tune after significant code changes:
		make clean && make device STEP=0x000  # baseline

		(test values in 0x40-byte steps, 64 total, modulo 0x1000):

			0x000 0x040 0x080 0x0C0 0x100 0x140 0x180 0x1C0
			0x200 0x240 0x280 0x2C0 0x300 0x340 0x380 0x3C0
			0x400 0x440 0x480 0x4C0 0x500 0x540 0x580 0x5C0
			0x600 0x640 0x680 0x6C0 0x700 0x740 0x780 0x7C0
			0x800 0x840 0x880 0x8C0 0x900 0x940 0x980 0x9C0
			0xA00 0xA40 0xA80 0xAC0 0xB00 0xB40 0xB80 0xBC0
			0xC00 0xC40 0xC80 0xCC0 0xD00 0xD40 0xD80 0xDC0
			0xE00 0xE40 0xE80 0xEC0 0xF00 0xF40 0xF80 0xFC0

		(find peak FPS, update linker default)

	Standard build uses linker default (no STEP= needed):
	  make device
*/

SECTIONS
{
	.text :
	{
		__text_start__ = .;
		*(.entry)
		build/libs/pdnewlib/pdnewlib.o(.text)

		. = ALIGN(32);
		build/src/script.o(.text)
		build/src/script.o(.text.*)

		. = ALIGN(0x1000); . += 0x5A0;
		*(.text.main)

		. = ALIGN(32);
		*(.text.tick)

		. = ALIGN(0x1000); . += 0x720;
		*(.text.cb)
		*(.text.cb_rare)

		. = ALIGN(32);
		build/*(.text)
		build/*(.text.*)

		. = ALIGN(32);
		/* libc */
		*(.text)
		*(.text.*)

		/* code to copy to TCM, seemingly runs faster */
		. = ALIGN(0x1000);
		. += 0xDE6;
        __itcm_dmg_start = .;
			*(.itcm.dmg.short)
			*(.itcm.dmg)
			. = ALIGN(32);
			*(.itcm.dmg.draw)
			. = ALIGN(32); . += 0x12;
			*(.itcm.dmg.fb)
			. = ALIGN(32); . += 0x6;
			*(.itcm.dmg.cb)
			. = ALIGN(32);
			*(.itcm.dmg.micro)
			*(.itcm.dmg.hdma)
			. = ALIGN(32);
			ITCM_STEP_PAD_DMG = DEFINED(ITCM_STEP_PAD_DMG) ? ITCM_STEP_PAD_DMG : 0x40;
			. += ITCM_STEP_PAD_DMG;
			*(.itcm.dmg.step)
			*(.itcm.dmg.*)
        __itcm_dmg_end = .;

		. = ALIGN(0x1000);
		. += 0xDE6;
        __itcm_cgb_start = .;
			*(.itcm.cgb.short)
			*(.itcm.cgb)
			. = ALIGN(32);
			*(.itcm.cgb.draw)
			. = ALIGN(32); . += 0x12;
			*(.itcm.cgb.fb)
			. = ALIGN(32); . += 0x6;
			*(.itcm.cgb.cb)
			. = ALIGN(32);
			*(.itcm.cgb.micro)
			*(.itcm.cgb.hdma)
			. = ALIGN(32);
			ITCM_STEP_PAD_CGB = DEFINED(ITCM_STEP_PAD_CGB) ? ITCM_STEP_PAD_CGB : 0x40;
			. += ITCM_STEP_PAD_CGB;
			*(.itcm.cgb.step)
			*(.itcm.cgb.*)
        __itcm_cgb_end = .;

		. = ALIGN(0x1000);
		__audio_start__ = .;
		*(.audio)
		*(.audio.*)

		. = ALIGN(32);
		__rare_start__ = .;
		*(.rare.cb)
		*(.rare)
		*(.rare.*)

		KEEP(*(.init))
		KEEP(*(.fini))

		/* .ctors */
		*crtbegin.o(.ctors)
		*crtbegin?.o(.ctors)
		*(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
		*(SORT(.ctors.*))
		*(.ctors)

		/* .dtors */
 		*crtbegin.o(.dtors)
 		*crtbegin?.o(.dtors)
 		*(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
 		*(SORT(.dtors.*))
 		*(.dtors)

		. = ALIGN(32);

		__rodata_start__ = .;
		/* separate the audio rodata for stability */
		build/minigb_apu/*(.rodata*)
		. = ALIGN(32); . += 0xC;
		*(.rodata*)

		KEEP(*(.eh_frame*))
	}

	.data :
	{
		__etext = .;

		. = ALIGN(32);

		__data_start__ = .;
		*(vtable)
		*(.data*)

		. = ALIGN(4);
		/* preinit data */
		PROVIDE_HIDDEN (__preinit_array_start = .);
		KEEP(*(.preinit_array))
		PROVIDE_HIDDEN (__preinit_array_end = .);

		. = ALIGN(4);
		/* init data */
		PROVIDE_HIDDEN (__init_array_start = .);
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
		PROVIDE_HIDDEN (__init_array_end = .);

		. = ALIGN(4);
		/* finit data */
		PROVIDE_HIDDEN (__fini_array_start = .);
		KEEP(*(SORT(.fini_array.*)))
		KEEP(*(.fini_array))
		PROVIDE_HIDDEN (__fini_array_end = .);

		. = ALIGN(4);
		/* All data end */
		__data_end__ = .;

	}

	.bss :
	{
		. = ALIGN(32);
		__bss_start__ = .;
		*(.bss*)
		*(COMMON)
		*(COM)
		. = ALIGN(4);
		__bss_end__ = .;
	}

  /DISCARD/ :
  {
		*(.ARM.exidx)
  }

}
