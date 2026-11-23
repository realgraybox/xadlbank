xadlbank
========

A small Xlib-based editor and preview tool for HMI/AdLib OPL2 soundbanks
using the .BNK format.

xadlbank is designed as a lightweight alternative to starting a full bank
editor when only a small amount of inspection or editing is required.


Features
--------

- Reads HMI/AdLib .BNK soundbank files directly.
- Writes edited banks directly back to .BNK format.
- No intermediate text file is required.
- Displays instrument number, data record index, name, and OPL parameter
  values.
- Edits instrument names.
- Edits the available instrument parameters.
- Shows raw BNK values in the inspector.
- Provides immediate OPL2 preview (single note or chord) through the
  external mop program.
- Built-in file chooser for loading another bank without restarting.
- Supports keyboard navigation.
- Supports mouse-assisted row selection and scrolling.
- Supports right-click preview.
- Supports simultaneous instances for comparing different banks.
- Uses a simple Xlib interface without a GUI toolkit.
- Byte-exact load/save round trips: only the bytes you edit change.
- Tolerates damaged or unusual banks instead of refusing them.
- Includes a built-in window icon.


Requirements
------------

- Linux or another Unix-like system with X11.
- An X11 server and DISPLAY environment.
- libX11 at build time.
- mop in PATH for instrument preview.

The program can still be used without mop, but the preview function
will not produce sound.


Usage
-----

    xadlbank [soundbank.bnk]

Example:

    xadlbank instruments.bnk

Without an argument xadlbank starts empty; press L to pick a bank.


Basic controls
--------------

    Up / Down       Select an instrument / inspector field
    Page Up/Down    Move through the instrument list
    Home / End      First / last instrument
    Left / Right    Switch inspector column
    Enter / Tab     Open the inspector / return to the list
    L / l           Load another bank (press twice to discard unsaved changes)
    N / n           Rename the selected instrument
    P / p / Space   Preview the selected instrument (one note)
    Shift+P         Preview the selected instrument as a C major chord
    S / s           Save the bank
    Q / q           Quit
    Esc             Cancel editing, return from the inspector, or quit

Mouse:

    Left button        Select a row or inspector field
    Mouse wheel        Scroll the list or adjust the selected inspector value
    Right button       Preview the selected instrument
    Shift+Right button Chord preview

The exact visible controls are also shown in the status line at the bottom
of the window.


Editing
-------

In list view, press N or n to edit the selected instrument name.

In inspector view:

- Use the arrow keys or left mouse button to select a parameter.
- Use +, -, [, or ] to increase or decrease the selected value.
- Use P, p, or the right mouse button to hear the current instrument.
- Press Escape or Enter to return to the list.

Changes are marked with [MODIFIED] in the title/header area.


Saving
------

Press S or s to write the current bank to the original input filename.

The whole file is kept in memory and edits are made to the bytes where
they live, so the header, offsets, table order, shared or unused data
records and any trailing bytes are written back unchanged.

Saving writes a temporary file next to the bank and renames it over the
original only when everything was written, so a failed save (full disk,
no permission) leaves the original file intact. File permissions are kept
and symbolic links are followed rather than replaced.

Keeping a backup copy of important soundbanks is still recommended.


Preview
-------

The preview function converts the selected instrument's 30 raw BNK
parameter bytes to hexadecimal and invokes:

    mop -r <instrument-data> -n 60 -d 1000             (note)
    mop -r <instrument-data> -n "60,64,67" -d 1500     (chord)

The preview uses the edited in-memory values, so parameter changes can be
heard immediately.


BNK format
----------

xadlbank currently targets HMI/AdLib BNK soundbanks containing:

- A 28-byte header.
- A name/index table.
- 30-byte instrument records.

The instrument records contain OPL2-related parameters for two operators,
including:

- Key-scale level.
- Frequency multiplier.
- Feedback.
- Attack, decay, sustain, and release.
- Output-level attenuation.
- Tremolo and vibrato flags.
- Key-scale rate.
- Connection/synthesis type.
- Waveform selection.

Some fields have format-specific meanings. Raw values are therefore shown
and retained where appropriate.

Each name entry points at a data record by index (the REC column). The
entries do not have to be in record order, and several entries may share
one record; editing a shared record affects every entry that uses it.

Unusual or damaged banks are handled as follows:

- Any number of entries (no fixed limit; tested with 15867).
- A 20-byte header (name table directly after the offset fields,
  without the 8 reserved bytes) is accepted as well as the usual 28.
- Parameter values outside the OPL2 range are shown and kept as they
  are; + only increases up to the range maximum.
- A name table cut short by the end of the file is clamped to the
  complete entries; a warning is shown in the status line.
- Entries pointing at a data record beyond the end of the file are
  listed as "missing data record" and are read-only.
- Unused entries (flag byte 0) are shown dimmed.
- Files without the "ADLIB-" signature, AdLib Gold BNK2 (OPL3) banks,
  empty banks and banks whose name table lies outside the file are
  refused with a message stating the reason. A failed load never
  replaces the bank that is already open.


Design goals
------------

xadlbank is intended to be:

- Small.
- Fast.
- Keyboard-friendly.
- Useful from a minimal Linux desktop.
- Independent of large GUI toolkits.
- Suitable for low-footprint or statically linked builds.
- Practical for inspecting and experimenting with legacy OPL2 banks.


Building
--------

The file chooser in xfileselect/ and the font library in xbmfont/ are
compiled into xadlbank.c directly, so only one source file is passed to
the compiler.

    make
    make install            (installs to /usr/bin, override with BINDIR=)

or by hand:

    gcc -O2 -Wall -Wextra -std=c99 -I./xbmfont xadlbank.c -lX11 -o xadlbank

Text is drawn with fonts built into the program (xbmfont, bundled in
xbmfont/: zlib licensed, the fonts are the public-domain X11 misc-fixed
fonts), so xadlbank looks the same on every X server and needs no server
fonts. The default is 6x13, which looks like X's own "fixed" font. For the
larger 9x15 font (about 7 KB more; the window opens wider to fit it):

    CFLAGS=-DPRETTYFONTS make

i386 targets are linked statically by default; use `make STATIC=` to
build dynamically, or `make STATIC=-static` to force a static build on
other targets. Static linking requirements vary between glibc, uClibc,
and musl systems (e.g. -lxcb -lXau -lXdmcp may be needed).


Status
------

Current format support:

    HMI/AdLib .BNK

Possible future additions:

- Additional OPL2 bank formats.
- OPL3 bank formats.
- More detailed format validation.
- Optional text export/import.
- Additional editing and display features.


License
-------

Modified zlib-style permissive license

xadlbank is distributed under the permissive license included at the top
of the source file and reproduced in this document.

/*
 * Copyright (C) 2026 M. Glargaard, aka graybox
 *
 * This software is provided "as-is", without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would
 *    be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not
 *    be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 */
