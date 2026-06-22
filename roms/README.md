# ROMs

The Amstrad PCW's boot code lives in the printer-controller MCU (a
mask-programmed 8041AH). At reset the MCU shifts 275 bytes into low
RAM through the parallel port; the Z80 then executes them as code,
which reads the first sector of drive A and jumps to it.

1985 ships those 275 bytes **statically compiled in** — see the
`PCW_BOOT_ROM[]` array near the top of `src/bootstrap.c`. They are a
verbatim copy of [Joyce](https://www.seasip.info/Unix/Joyce/)'s
`pcw_boot.rom` (which also matches MAME's PCW printer-MCU dump and
ZEsarUX's bundled boot ROM), so no external firmware files are needed
and this directory ships empty.

The CP/M Plus operating system itself lives on a CF2 disk image
(180 KB single-sided 3"). Mount one with
`./1985 --disk-a /path/to/cpm.dsk`.
