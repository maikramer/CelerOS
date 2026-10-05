/* qoaenc — WAV PCM16 -> QOA pela implementacao de referencia (phoboslab/qoa,
 * MIT). O firmware decodifica o MESMO header (main/Hardware/Audio/qoa.h);
 * aqui so o encoder interessa. Compilar: gcc -O2 -o qoaenc qoaenc.c
 * Uso: qoaenc entrada.wav saida.qoa */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QOA_IMPLEMENTATION
#include "../../main/Hardware/Audio/qoa.h"

static unsigned rd32(const unsigned char* p) { return p[0]|(p[1]<<8)|(p[2]<<16)|((unsigned)p[3]<<24); }
static unsigned rd16(const unsigned char* p) { return p[0]|(p[1]<<8); }

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "uso: %s in.wav out.qoa\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("wav"); return 1; }
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        fprintf(stderr, "nao e WAV\n"); return 1;
    }
    unsigned ch = 0, rate = 0; long dataOff = 0; unsigned dataBytes = 0;
    for (;;) {
        unsigned char c[8];
        if (fread(c, 1, 8, f) != 8) break;
        unsigned sz = rd32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char fmt[16];
            if (fread(fmt, 1, 16, f) != 16) break;
            if (rd16(fmt) != 1 || rd16(fmt + 14) != 16) { fprintf(stderr, "so PCM16\n"); return 1; }
            ch = rd16(fmt + 2); rate = rd32(fmt + 4);
            if (sz > 16) fseek(f, sz - 16, SEEK_CUR);
        } else if (!memcmp(c, "data", 4)) {
            dataBytes = sz; dataOff = ftell(f);
            if (sz & 1) sz--;  /* padding */
        } else {
            fseek(f, sz + (sz & 1), SEEK_CUR);
        }
        if (ch && dataBytes) break;
    }
    if (!ch || !dataBytes) { fprintf(stderr, "WAV sem fmt/data\n"); return 1; }
    fseek(f, dataOff, SEEK_SET);
    unsigned frames = dataBytes / (2 * ch);
    short* pcm = malloc((size_t)frames * ch * 2);
    if (fread(pcm, 2 * ch, frames, f) != frames) { fprintf(stderr, "wav truncado\n"); return 1; }
    fclose(f);

    qoa_desc d = {0};
    d.channels = ch;
    d.samplerate = rate;
    d.samples = frames;
    unsigned len = 0;
    void* out = qoa_encode(pcm, &d, &len);
    free(pcm);
    if (!out) { fprintf(stderr, "encode falhou\n"); return 1; }
    FILE* o = fopen(argv[2], "wb");
    if (!o) { perror("qoa"); return 1; }
    fwrite(out, 1, len, o);
    fclose(o);
    free(out);
    fprintf(stderr, "%s: %u Hz ch=%u %u amostras -> %u bytes QOA\n", argv[2], rate, ch, frames, len);
    return 0;
}
