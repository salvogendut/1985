#include "crtc.h"
#include <string.h>

void crtc_init(Crtc *c) {
    memset(c, 0, sizeof(*c));
}

void crtc_reset(Crtc *c) {
    c->frame_count = 0;
}

bool crtc_frame(Crtc *c) {
    c->frame_count++;
    return true;   /* every emulated frame ends with VSYNC */
}
