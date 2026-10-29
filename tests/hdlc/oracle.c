#include "hdlc_parser.h"
#include "hdlc_frame.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void SM_onAssert(char const *module, int line) {
    fprintf(stderr, "assert %s:%d\n", module, line);
    abort();
}
static void emit(void *ctx, uint8_t const *frame, size_t size) {
    (void)ctx;
    assert(putchar(size >> 8) != EOF && putchar(size & 255U) != EOF);
    assert(fwrite(frame, 1U, size, stdout) == size);
}
#ifndef HDLC_IMPORTED_TEST
int main(int argc, char **argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "encode") == 0) {
        uint8_t raw[258], wire[522];
        size_t n = fread(raw, 1U, sizeof(raw), stdin);
        assert(n >= 3U && n == 3U + raw[2]);
        n = HdlcFrame_encode(raw[0], raw[1], raw + 3U, raw[2],
                             wire, sizeof(wire));
        assert(n != 0U && fwrite(wire, 1U, n, stdout) == n);
        return 0;
    }
    HdlcParser parser;
    HdlcParser_ctor(&parser, &emit, NULL);
    HdlcParser_init(&parser);
    int byte;
    while ((byte = getchar()) != EOF) {
        HdlcParser_input(&parser, byte);
    }
    return 0;
}
#endif
