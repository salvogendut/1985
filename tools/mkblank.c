/* mkblank — create a freshly-formatted PCW DSK image.
 *
 * Standalone CLI wrapper around disk_create_blank() so headless tests can
 * produce CF2 / CF2DD blanks without driving the overlay. */

#include <stdio.h>
#include <string.h>
#include "../src/disk.h"

extern int disk_create_blank(const char *path, DiskType type);

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s PATH cf2|cf2dd\n", argv[0]);
        return 2;
    }
    DiskType t;
    if      (!strcmp(argv[2], "cf2"))   t = DISK_TYPE_CF2;
    else if (!strcmp(argv[2], "cf2dd")) t = DISK_TYPE_CF2DD;
    else { fprintf(stderr, "unknown type %s\n", argv[2]); return 2; }
    if (disk_create_blank(argv[1], t) != 0) return 1;
    fprintf(stderr, "%s: %s blank written\n", argv[1], argv[2]);
    return 0;
}
