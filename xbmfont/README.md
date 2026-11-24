# xbmfont (bundled)

Embedded bitmap fonts for Xlib, from <https://github.com/realgraybox/xbmfont>
(commit 7e9b277): `xbmfont.h` and the fonts xadlbank uses (6x13 and 10x20, or
9x15 for `PRETTYFONTS` builds), unchanged. xadlbank and its file chooser draw all text
with these, so they look the same on every X server.

The library is zlib licensed (see `LICENSE`). The fonts are the X11 misc-fixed
fonts, which are public domain; see the xbmfont repository's `fonts/README.md`
for their sources.
