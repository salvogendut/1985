# ROMs

The Amstrad PCW has **no boot ROM**. At reset the hardware injects a
778-byte bootstrap stream into the Z80 instruction fetcher, which
builds a 256-byte loader at 0x0002. That loader reads the first sector
of drive A and jumps to it.

1985 emulates this bootstrap stream in `src/bootstrap.c`, so no
firmware ROM files are needed and this directory ships empty.

The CP/M Plus operating system itself lives on a CF2 disk image
(720 KB, 3.5"). Mount one with `./1985 --disk-a /path/to/cpm.dsk`.
