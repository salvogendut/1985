#pragma once
#include "types.h"

/* .sna load/save — stubbed until the snapshot format is wired up.
 * The CLI plumbing (`--load-sna`, `--save-sna-at`) calls through these
 * so the args parse but the actual operation always fails. */

struct PCW;

int snapshot_load(struct PCW *pcw, const char *path);
int snapshot_save(struct PCW *pcw, const char *path);
