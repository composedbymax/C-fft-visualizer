#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define WIDTH 80
#define HEIGHT 20
static int32_t read_sample(const uint8_t *buf, uint16_t bps) {
    if (bps == 16) {
        int16_t v;
        memcpy(&v, buf, 2);
        return v;
    } else if (bps == 24) {
        int32_t v = (buf[0] | (buf[1] << 8) | (buf[2] << 16));
        if (v & 0x800000) v |= 0xFF000000;
        return v;
    }
    return 0;
}
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.wav>\n", argv[0]);
        return 1;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror("Open"); return 1; }
    char riff[4]; uint32_t riff_size; char wave[4];
    fread(riff, 1, 4, fp);
    fread(&riff_size, 4, 1, fp);
    fread(wave, 1, 4, fp);
    if (strncmp(riff, "RIFF", 4) || strncmp(wave, "WAVE", 4)) {
        fprintf(stderr, "Not a valid WAV file\n");
        fclose(fp);
        return 1;
    }
    uint16_t audioFormat=0, numCh=0, bits=0;
    uint32_t sampleRate=0, dataBytes=0;
    char chunkID[5]; uint32_t chunkSize;
    while (fread(chunkID, 1, 4, fp) == 4) {
        chunkID[4] = '\0';
        fread(&chunkSize, 4, 1, fp);
        if (strcmp(chunkID, "fmt ") == 0) {
            fread(&audioFormat, 2, 1, fp);
            fread(&numCh, 2, 1, fp);
            fread(&sampleRate, 4, 1, fp);
            fseek(fp, 6, SEEK_CUR);
            fread(&bits, 2, 1, fp);
            fseek(fp, chunkSize - 16, SEEK_CUR);
        } else if (strcmp(chunkID, "data") == 0) {
            dataBytes = chunkSize;
            break;
        } else {
            fseek(fp, chunkSize, SEEK_CUR);
        }
    }
    if (audioFormat != 1 || (bits != 16 && bits != 24)) {
        fprintf(stderr, "Unsupported WAV format: PCM=%d, bits=%d\n", audioFormat, bits);
        fclose(fp);
        return 1;
    }
    uint32_t bytesPerSample = bits / 8;
    uint32_t totalSamples = dataBytes / bytesPerSample;
    if (totalSamples == 0) {
        fprintf(stderr, "No audio data found\n");
        fclose(fp);
        return 1;
    }
    uint8_t *buf = malloc(dataBytes);
    if (!buf) { perror("Alloc"); fclose(fp); return 1; }
    fread(buf, 1, dataBytes, fp);
    fclose(fp);
    char canvas[HEIGHT][WIDTH];
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            canvas[y][x] = ' ';
    int32_t maxAmp = 1;
    for (uint32_t i = 0; i < totalSamples; i++) {
        int32_t v = read_sample(buf + i * bytesPerSample, bits);
        if (abs(v) > maxAmp) maxAmp = abs(v);
    }
    uint32_t perCol = totalSamples / WIDTH;
    for (int col = 0; col < WIDTH; col++) {
        uint32_t start = col * perCol;
        uint32_t end = start + perCol;
        if (end > totalSamples) end = totalSamples;
        int32_t peak = 0;
        for (uint32_t i = start; i < end; i++) {
            int32_t v = read_sample(buf + i * bytesPerSample, bits);
            if (abs(v) > peak) peak = abs(v);
        }
        int row = (int)(((double)peak / maxAmp) * (HEIGHT/2 - 1));
        int cen = HEIGHT / 2;
        if (cen + row < HEIGHT) canvas[cen + row][col] = '*';
        if (cen - row >= 0)      canvas[cen - row][col] = '*';
    }
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) putchar(canvas[y][x]);
        putchar('\n');
    }
    free(buf);
    return 0;
}