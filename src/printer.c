#include "printer.h"
#include <string.h>

void printer_init(Printer *p) {
    memset(p, 0, sizeof(*p));
}

u8 printer_read(Printer *p, u8 port) {
    (void)p; (void)port;
    return 0xFF;   /* "ready, no error" */
}

void printer_write(Printer *p, u8 port, u8 val) {
    (void)p; (void)port; (void)val;
}
