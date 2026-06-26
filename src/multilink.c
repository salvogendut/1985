/* PCW Multilink probe-stub. See multilink.h for context. */
#include "multilink.h"
#include <string.h>

/* The fixed "ring broken" sequence from Seasip §14.1. Multilink-aware
 * software reads this on probe and concludes "no functioning ring",
 * then stops polling. */
static const u8 PROBE_REPLY[4] = { 0x00, 0x90, 0x99, 0x00 };

void multilink_init(Multilink *m) {
    memset(m, 0, sizeof(*m));
}

void multilink_reset(Multilink *m) {
    m->idx = 0;
    /* present persists across warm reset — same as cps.present. */
}

void multilink_set_present(Multilink *m, bool present) {
    if (m->present != present) m->idx = 0;
    m->present = present;
}

u8 multilink_read(Multilink *m, u8 port) {
    (void)port;   /* Multilink decodes both 0xA6 and 0xA7 the same way */
    u8 v = PROBE_REPLY[m->idx & 0x03];
    m->idx = (u8)((m->idx + 1) & 0x03);
    return v;
}

void multilink_write(Multilink *m, u8 port, u8 val) {
    (void)m; (void)port; (void)val;   /* drop; nothing to emulate */
}
