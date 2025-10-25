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
