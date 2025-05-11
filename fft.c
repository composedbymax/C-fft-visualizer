#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#define WIDTH 80
#define HEIGHT 20
#define CHUNK_FRAMES 1024
#define PI 3.14159265358979323846
#define MAX(a,b) ((a) > (b) ? (a) : (b))
static void bit_reverse(double *real, double *imag, int n) {
    int j = 0;
    for (int i = 1; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) {
            double t = real[i]; real[i] = real[j]; real[j] = t;
            t = imag[i]; imag[i] = imag[j]; imag[j] = t;
        }
    }
}
static void fft(double *real, double *imag, int n) {
    bit_reverse(real, imag, n);
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2 * PI / len;
        double wlen_r = cos(ang);
        double wlen_i = sin(ang);
        for (int i = 0; i < n; i += len) {
            double ur = 1, ui = 0;
            for (int j = 0; j < len/2; j++) {
                int u = i + j;
                int v = i + j + len/2;
                double vr = real[v] * ur - imag[v] * ui;
                double vi = real[v] * ui + imag[v] * ur;
                real[v] = real[u] - vr;
                imag[v] = imag[u] - vi;
                real[u] += vr;
                imag[u] += vi;
                double nxt_r = ur * wlen_r - ui * wlen_i;
                ui = ur * wlen_i + ui * wlen_r;
                ur = nxt_r;
            }
        }
    }
}
static uint16_t read_le16(const uint8_t *b) { return b[0] | (b[1] << 8); }
static uint32_t read_le32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24); }
static int32_t read_le24(const uint8_t *b) {
    int32_t v = b[0] | (b[1] << 8) | (b[2] << 16);
    if (v & 0x800000) v |= ~0xFFFFFF;
    return v;
}
int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <input.wav>\n", argv[0]); return 1; }
    FILE *fp = fopen(argv[1], "rb"); if (!fp) { perror("fopen"); return 1; }
    char riff[4], wave[4], chunkID[5] = {0};
    uint8_t sizeBuf[4];
    fread(riff,1,4,fp); fseek(fp,4,SEEK_CUR); fread(wave,1,4,fp);
    uint16_t audioFormat=0, numCh=0, bits=0;
    uint32_t sampleRate=0, dataBytes=0;
    while (fread(chunkID,1,4,fp)==4) {
        fread(sizeBuf,4,1,fp);
        uint32_t chunkSize = read_le32(sizeBuf);
        if (!memcmp(chunkID,"fmt ",4)) {
            uint8_t fmt[16]; fread(fmt,1,16,fp);
            audioFormat = read_le16(fmt);
            numCh       = read_le16(fmt+2);
            sampleRate  = read_le32(fmt+4);
            bits        = read_le16(fmt+14);
            fseek(fp, chunkSize-16, SEEK_CUR);
        } else if (!memcmp(chunkID,"data",4)) {
            dataBytes = chunkSize; break;
        } else fseek(fp, chunkSize, SEEK_CUR);
    }
    if (audioFormat!=1 || (bits!=8 && bits!=16 && bits!=24)) {
        fprintf(stderr,"Unsupported format PCM=%d bits=%d\n",audioFormat,bits);
        return 1;
    }
    uint32_t bps = bits/8, frameBytes = bps * numCh;
    size_t totalFrames = dataBytes / frameBytes;
    uint8_t *buf = malloc(CHUNK_FRAMES * frameBytes);
    double *real = calloc(CHUNK_FRAMES, sizeof(double));
    double *imag = calloc(CHUNK_FRAMES, sizeof(double));
    double maxMag = 1;
    for (size_t frames=0; frames < totalFrames; frames += CHUNK_FRAMES) {
        size_t toRead = (frames + CHUNK_FRAMES <= totalFrames) ? CHUNK_FRAMES : totalFrames - frames;
        fread(buf, frameBytes, toRead, fp);
        for (size_t i=0; i<CHUNK_FRAMES; i++) {
            double sample = 0;
            if (i < toRead) {
                uint8_t *p = buf + i*frameBytes;
                if (bits==8) {
                    int v = (int)p[0] - 128;
                    sample = v;
                    if (numCh==2) sample = ((int)p[0]-128 + (int)p[1]-128)/2.0;
                } else if (bits==16) {
                    int16_t v1 = p[0] | (p[1]<<8);
                    int16_t v2 = numCh==2 ? (p[2] | (p[3]<<8)) : 0;
                    sample = (v1 + v2)/2.0;
                } else {
                    int32_t v1 = read_le24(p);
                    int32_t v2 = numCh==2? read_le24(p+3) : 0;
                    sample = (v1 + v2)/2.0;
                }
            }
            real[i] = sample;
            imag[i] = 0;
        }
        fft(real, imag, CHUNK_FRAMES);
        int bins = CHUNK_FRAMES/2;
        double mag[bins];
        for (int i=0; i<bins; i++) {
            mag[i] = sqrt(real[i]*real[i] + imag[i]*imag[i]);
            if (mag[i] > maxMag) maxMag = mag[i];
        }
        printf("\x1b[H");
        for (int y=0; y<HEIGHT; y++) {
            for (int x=0; x<WIDTH; x++) {
                int idx = x * bins / WIDTH;
                double norm = mag[idx] / maxMag;
                int level = norm * HEIGHT;
                putchar((HEIGHT - y) <= level ? '#' : ' ');
            }
            putchar('\n');
        }
        fflush(stdout);
        usleep((useconds_t)(CHUNK_FRAMES * 1e6 / sampleRate));
    }
    printf("Done\n");
    free(buf); free(real); free(imag); fclose(fp);
    return 0;
}