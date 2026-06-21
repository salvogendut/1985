#include "snapshot.h"
#include <stdio.h>

int snapshot_load(struct PCW *pcw, const char *path) {
    (void)pcw;
    fprintf(stderr, "snapshot: load %s — not implemented\n", path);
    return -1;
}

int snapshot_save(struct PCW *pcw, const char *path) {
    (void)pcw;
    fprintf(stderr, "snapshot: save %s — not implemented\n", path);
    return -1;
}
