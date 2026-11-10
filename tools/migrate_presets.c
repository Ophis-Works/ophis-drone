/* tools/migrate_presets.c — ONE-OFF dev-time migration (version-2 gate):
 * rewrites preset files to the current format by parse -> write
 * round-trip. The input must parse as schema version 2 — the parser's
 * version gate (schema_parse.c) rejects version 1 files LOUDLY, this
 * tool is not a v1 converter. Absent optionals get their defaults
 * (xmod tgt 0 / mix 0, chain mixvol 100); chfm / dest "mix" are also
 * rejected by the parser, so any file still carrying them FAILS LOUDLY
 * instead of being silently migrated.
 * Usage: migrate_presets file.json [more.json ...]  (rewrites in place)
 * Not shipped — a development tool. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "preset/schema.h"

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) { perror(argv[i]); return 1; }
        static char in[65536], out[65536];
        int n = (int)fread(in, 1, sizeof in - 1, f);
        fclose(f);
        in[n] = 0;
        synth_params_t p1, p2;
        char err[256];
        memset(&p1, 0, sizeof p1);
        if (schema_parse_params(in, n, &p1, err, sizeof err) != 0) {
            fprintf(stderr, "FAIL %s: does not parse: %s\n", argv[i], err);
            return 1;
        }
        int w = schema_write_params(out, sizeof out, &p1);
        if (w <= 0) { fprintf(stderr, "FAIL %s: write\n", argv[i]); return 1; }
        memset(&p2, 0, sizeof p2);
        if (schema_parse_params(out, w, &p2, err, sizeof err) != 0 ||
            memcmp(&p1, &p2, sizeof p1) != 0) {
            fprintf(stderr, "FAIL %s: round-trip mismatch\n", argv[i]);
            return 1;
        }
        f = fopen(argv[i], "wb");
        if (!f) { perror(argv[i]); return 1; }
        fwrite(out, 1, (size_t)w, f);
        fclose(f);
        printf("%s: migrated (%d bytes, params verified)\n", argv[i], w);
    }
    return 0;
}
