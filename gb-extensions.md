# Game Boy IO Extensions for Playdate

To allow game boy games access to Playdate features such as the crank and accelerometer,
CrankBoy includes some custom I/O registers for this purpose. They must first be enabled before use (`FF57`)

## `FF57` [w] - Features Enable

Different bits enable/disable different features:

```
bit 0 - accelerometer
bit 1 - xram
bit 2 - crank menu mode
bits 3-7 - reserved (assume 0 as safe default)
```

If bit 0 is set, accelerometer is enabled; otherwise it is disabled.

If bit 1 is set, RAM range 0xFEA0-0xFEFF becomes a read-writeable 0x60-byte region. (Normally this region is unused.) Together with a C script, this area can function as a trainer, to hold shared variables, or to store state which is included in the save file.

If bit 2 is set, the crank register `FF58` behaves as a menu-indexer. As the user cranks, it increments/decrements each time the next/previous menu item should be selected, and it can be reset to 0 by writing to it. (See below.)

## `FF57` [r] - Crank Docked

When reading from `FF57`, some flags are returned:

```
bit 0: crank docked [1 if docked]
bits 1-7: reserved
```

## `FF58`/`FF59` [r] - Crank

_If crank menu mode is disabled:_

16-bit word, contains a value in the range 0-FFFF, indicates crank rotation. FFFF is one pip less than 360°.

## `FF58` [r] - Crank menu delta

_If crank menu mode is enabled:_

Accumulates menu movement; as the user cranks, this increments, and as the user cranks, it decrements, such that a delta of 1 should correspond to 1 menu movement (with +'ve meaning move up, and -'ve meaning move down).

## `FF59` [r] - Crank menu delta

_If crank menu mode is enabled:_

Accumulates menu movement; as the user cranks, this increments, and as the user cranks, it decrements, such that a delta of 1 should correspond to 1 menu movement (with +'ve meaning move up, and -'ve meaning move down).

## `FF5A` [r] - Crank menu delta accumulation

_If crank menu mode is enabled:_

A value in the range 1 to 0xFF indicating how close the crank is to overflowing and incrementing/decrementing crank menu delta.

## `FF58` [w]

Writing any value here causes the crank menu delta to reset to 0

## `FF59` [w]

Writing any value here causes the crank menu delta _accumulation_ to reset.

## `FF5A` and `FF5B` - Accelerometer (X axis)

16-bit word. 8000 = zero acceleration; A000 = accelerating to the left at _g_; 6000 = accelerating to the right at _g_. Limited to the range [-4g, 4g).

## `FF5C` and `FF5D` - Accelerometer (Y axis)

16-bit word. 8000 = zero acceleration; A000 = accelerating upward at _g_ (as would be the case if the user is holding the playdate vertically); 6000 = accelerating to the downward at _g_ (as if the user is holding the playdate upside-down). Limited to the range [-4g, 4g).

## `FF5E` and `FF5F` - Accelerometer (Z axis)

16-bit word. 8000 = zero acceleration; A000 = accelerating outward from the screen at _g_ (as would be the case if the playdate is lying on its back); 6000 = accelerating to the toward the screen at _g_ (as if the playdate is lying on its front). Limited to the range [-4g, 4g).
