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
- Displays instrument number, MIDI index, name, and OPL parameter values.
- Edits instrument names.
- Edits the available instrument parameters.
- Shows raw BNK values in the inspector.
- Provides immediate OPL2 preview through the external opl2play program.
- Supports keyboard navigation.
- Supports mouse-assisted row selection and scrolling.
- Supports right-click preview.
- Supports simultaneous instances for comparing different banks.
- Uses a simple Xlib interface without a GUI toolkit.
- Preserves unmodified bank data during load/save round trips.
- Includes a built-in window icon.


Requirements
------------

- Linux or another Unix-like system with X11.
- An X11 server and DISPLAY environment.
- libX11 at build time.
- opl2play in PATH for instrument preview.

The program can still be used without opl2play, but the preview function
will not produce sound.


Usage
-----

    xadlbank <soundbank.bnk>

Example:

    xadlbank instruments.bnk


Basic controls
--------------

    Up / Down       Select an instrument
    Page Up/Down    Move through the instrument list
    Left / Right    Select an inspector field
    Enter           Open the inspector / return to the list
    N / n           Rename the selected instrument
    P / p / Space   Preview the selected instrument
    S / s           Save the bank
    Q / q           Quit
    Esc             Cancel editing or return from the inspector

Mouse:

    Left button     Select a row or inspector field
    Mouse wheel     Scroll the list or adjust the selected inspector value
    Right button    Preview the selected instrument

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

It is recommended to keep a backup copy of important soundbanks before
editing. The save operation writes the binary BNK file directly.


Preview
-------

The preview function converts the selected instrument's 30 raw BNK
parameter bytes to hexadecimal and invokes:

    opl2play -r <instrument-data> -n 60 -d 1000

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

A typical dynamic build is:

    gcc -O2 -Wall -Wextra -std=c99 xadlbank.c -lX11 -o xadlbank

For a static build, use the appropriate static C library and X11
development libraries available on the target system. Static linking
requirements vary between glibc, uClibc, and musl systems.


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
