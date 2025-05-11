#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#define WIDTH 80
#define CHUNK_FRAMES 1024
#define MAX(a,b) ((a) > (b) ? (a) : (b))
static uint16_t read_le16(const uint8_t *b) { return b[0] | (b[1] << 8); }
static uint32_t read_le32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24); }
static int32_t read_le24(const uint8_t *b) {
    int32_t v = b[0] | (b[1] << 8) | (b[2] << 16);
    if (v & 0x800000) v |= ~0xFFFFFF;
    return v;
}
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.wav>\n", argv[0]);
        return 1;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror("fopen"); return 1; }
    char riff[4], wave[4];
    if (fread(riff,1,4,fp)!=4 || memcmp(riff,"RIFF",4)!=0) { fprintf(stderr,"Not RIFF\n"); fclose(fp); return 1; }
    fseek(fp,4,SEEK_CUR); // skip size
    if (fread(wave,1,4,fp)!=4 || memcmp(wave,"WAVE",4)!=0) { fprintf(stderr,"Not WAVE\n"); fclose(fp); return 1; }
    uint16_t audioFormat=0, numCh=0, bits=0;
    uint32_t sampleRate=0, dataBytes=0;
    char chunkID[5] = {0};
    uint8_t sizeBuf[4];
    while (fread(chunkID,1,4,fp)==4) {
        if (fread(sizeBuf,4,1,fp)!=1) break;
        uint32_t chunkSize = read_le32(sizeBuf);
        if (memcmp(chunkID, "fmt ", 4) == 0) {
            uint8_t fmtbuf[16];
            fread(fmtbuf,1,16,fp);
            audioFormat = read_le16(fmtbuf);
            numCh       = read_le16(fmtbuf+2);
            sampleRate  = read_le32(fmtbuf+4);
            bits        = read_le16(fmtbuf+14);
            fseek(fp, chunkSize - 16, SEEK_CUR);
        }
        else if (memcmp(chunkID, "data", 4) == 0) {
            dataBytes = chunkSize;
            break;
        } else {
            fseek(fp, chunkSize, SEEK_CUR);
        }
    }
    if (audioFormat != 1 || (bits!=8 && bits!=16 && bits!=24)) {
        fprintf(stderr,"Unsupported format: PCM=%d bits=%d\n", audioFormat, bits);
        fclose(fp);
        return 1;
    }
    uint32_t bytesPerSample = bits/8;
    uint32_t frameBytes = bytesPerSample * numCh;
    size_t totalFrames = dataBytes / frameBytes;
    size_t framesRead = 0;
    size_t chunkBytes = CHUNK_FRAMES * frameBytes;
    uint8_t *chunkBuf = malloc(chunkBytes);
    if (!chunkBuf) { perror("malloc"); fclose(fp); return 1; }
    int maxAmp = 1;
    long dataStart = ftell(fp);
    while (framesRead < totalFrames) {
        size_t toRead = (totalFrames - framesRead < CHUNK_FRAMES) ? totalFrames - framesRead : CHUNK_FRAMES;
        size_t got = fread(chunkBuf, frameBytes, toRead, fp);
        for (size_t i = 0; i < got; i++) {
            int amp = 0;
            uint8_t *p = chunkBuf + i * frameBytes;
            if (bits == 8) {
                int v1 = abs(p[0] - 128);
                int v2 = (numCh==2) ? abs(p[1] - 128) : 0;
                amp = MAX(v1,v2);
            } else if (bits == 16) {
                int16_t v1 = (int16_t)(p[0] | (p[1]<<8));
                int16_t v2 = (numCh==2) ? (int16_t)(p[2] | (p[3]<<8)) : 0;
                amp = abs(v1);
                amp = MAX(amp, abs(v2));
            } else {
                int32_t v1 = read_le24(p);
                int32_t v2 = (numCh==2) ? read_le24(p+3) : 0;
                amp = abs(v1);
                amp = MAX(amp, abs(v2));
            }
            maxAmp = MAX(maxAmp, amp);
        }
        framesRead += got;
    }
    fseek(fp, dataStart, SEEK_SET);
    framesRead = 0;
    double secPerFrame = 1.0 / sampleRate;
    double secPerChunk = secPerFrame * CHUNK_FRAMES;
    unsigned int sleepUs = (unsigned int)(secPerChunk * 1e6);
    while (framesRead < totalFrames) {
        size_t toRead = (totalFrames - framesRead < CHUNK_FRAMES) ? totalFrames - framesRead : CHUNK_FRAMES;
        size_t got = fread(chunkBuf, frameBytes, toRead, fp);
        int peak = 0;
        for (size_t i = 0; i < got; i++) {
            int amp = 0;
            uint8_t *p = chunkBuf + i * frameBytes;
            if (bits == 8) {
                int v1 = abs(p[0] - 128);
                int v2 = (numCh==2) ? abs(p[1] - 128) : 0;
                amp = MAX(v1,v2);
            } else if (bits == 16) {
                int16_t v1 = (int16_t)(p[0] | (p[1]<<8));
                int16_t v2 = (numCh==2) ? (int16_t)(p[2] | (p[3]<<8)) : 0;
                amp = abs(v1);
                amp = MAX(amp, abs(v2));
            } else {
                int32_t v1 = read_le24(p);
                int32_t v2 = (numCh==2) ? read_le24(p+3) : 0;
                amp = abs(v1);
                amp = MAX(amp, abs(v2));
            }
            peak = MAX(peak, amp);
        }
        int len = (int)((double)peak / maxAmp * WIDTH);
        printf("\r[");
        for (int i = 0; i < len; i++) putchar('#');
        for (int i = len; i < WIDTH; i++) putchar(' ');
        printf("]"); fflush(stdout);
        usleep(sleepUs);
        framesRead += got;
    }
    printf("\nDone.\n");
    free(chunkBuf);
    fclose(fp);
    return 0;
}