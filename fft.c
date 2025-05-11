#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#define WIDTH        80
#define HEIGHT       10
#define CHUNK_FRAMES 1024
#define MAX(a,b)     ((a) > (b) ? (a) : (b))
static uint16_t read_le16(const uint8_t *b) {
    return b[0] | (b[1] << 8);
}
static uint32_t read_le32(const uint8_t *b) {
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}
static int32_t read_le24(const uint8_t *b) {
    int32_t v = b[0] | (b[1] << 8) | (b[2] << 16);
    if (v & 0x800000) v |= ~0xFFFFFF;
    return v;
}
void fft(double *real, double *imag, int n) {
    if (n <= 1) return;
    double even_r[n/2], even_i[n/2],
           odd_r[n/2], odd_i[n/2];
    for (int i = 0; i < n/2; i++) {
        even_r[i] = real[2*i];
        even_i[i] = imag[2*i];
        odd_r[i]  = real[2*i+1];
        odd_i[i]  = imag[2*i+1];
    }
    fft(even_r, even_i, n/2);
    fft(odd_r,  odd_i,  n/2);
    for (int i = 0; i < n/2; i++) {
        double angle = 2*M_PI*i/n;
        double cosf =  cos(angle), sinf = sin(angle);
        double tr =  cosf*odd_r[i] + sinf*odd_i[i];
        double ti =  cosf*odd_i[i] - sinf*odd_r[i];
        real[i]       = even_r[i] + tr;
        imag[i]       = even_i[i] + ti;
        real[i+n/2]   = even_r[i] - tr;
        imag[i+n/2]   = even_i[i] - ti;
    }
}
void normalize_bins(double *bins, int num_bins, int max_height) {
    double m = 0;
    for (int i = 0; i < num_bins; i++)
        if (bins[i] > m) m = bins[i];
    if (m == 0) return;
    for (int i = 0; i < num_bins; i++)
        bins[i] = (bins[i] / m) * (double)max_height;
}
static char grid[HEIGHT][WIDTH+1];
void init_grid(void) {
    for (int r = 0; r < HEIGHT; r++) {
        memset(grid[r], ' ', WIDTH);
        grid[r][WIDTH] = '\0';
    }
}
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.wav>\n", argv[0]);
        return 1;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror("fopen"); return 1; }
    char riff[4], wave[4];
    if (fread(riff,1,4,fp)!=4 || memcmp(riff,"RIFF",4)) {
        fprintf(stderr,"Not RIFF\n"); fclose(fp); return 1;
    }
    fseek(fp,4,SEEK_CUR);
    if (fread(wave,1,4,fp)!=4 || memcmp(wave,"WAVE",4)) {
        fprintf(stderr,"Not WAVE\n"); fclose(fp); return 1;
    }
    uint16_t audioFormat=0, numCh=0, bits=0;
    uint32_t sampleRate=0, dataBytes=0;
    char chunkID[5] = {0};
    uint8_t sizeBuf[4];
    while (fread(chunkID,1,4,fp)==4) {
        if (fread(sizeBuf,4,1,fp)!=1) break;
        uint32_t chunkSize = read_le32(sizeBuf);
        if (!memcmp(chunkID,"fmt ",4)) {
            uint8_t fmtbuf[16];
            fread(fmtbuf,1,16,fp);
            audioFormat = read_le16(fmtbuf);
            numCh       = read_le16(fmtbuf+2);
            sampleRate  = read_le32(fmtbuf+4);
            bits        = read_le16(fmtbuf+14);
            fseek(fp, chunkSize - 16, SEEK_CUR);
        }
        else if (!memcmp(chunkID,"data",4)) {
            dataBytes = chunkSize;
            break;
        }
        else {
            fseek(fp, chunkSize, SEEK_CUR);
        }
    }
    if (audioFormat!=1 || (bits!=8 && bits!=16 && bits!=24)) {
        fprintf(stderr,"Unsupported format: PCM=%d bits=%d\n",audioFormat,bits);
        fclose(fp);
        return 1;
    }
    uint32_t bytesPerSample = bits/8;
    uint32_t frameBytes     = bytesPerSample * numCh;
    size_t   totalFrames    = dataBytes / frameBytes;
    size_t   framesRead     = 0;
    size_t   chunkBytes     = CHUNK_FRAMES * frameBytes;
    uint8_t *chunkBuf = malloc(chunkBytes);
    if (!chunkBuf) { perror("malloc"); fclose(fp); return 1; }
    double real[CHUNK_FRAMES], imag[CHUNK_FRAMES];
    double bins[CHUNK_FRAMES/2];
    double secPerFrame = 1.0 / sampleRate;
    double secPerChunk = secPerFrame * CHUNK_FRAMES;
    unsigned int sleepUs = (unsigned int)(secPerChunk * 1e6);
    init_grid();
    printf("\x1b[2J");
    printf("\x1b[?25l");
    while (framesRead < totalFrames) {
        size_t toRead = (totalFrames - framesRead < CHUNK_FRAMES)
                      ? totalFrames - framesRead
                      : CHUNK_FRAMES;
        size_t got = fread(chunkBuf, frameBytes, toRead, fp);
        if (got == 0) break;
        for (size_t i = 0; i < CHUNK_FRAMES; i++) {
            if (i >= toRead) {
                real[i] = imag[i] = 0.0;
            } else if (bits == 8) {
                real[i] = (double)(chunkBuf[i*frameBytes] - 128);
                imag[i] = 0.0;
            } else if (bits == 16) {
                int16_t s = chunkBuf[i*frameBytes] | (chunkBuf[i*frameBytes+1]<<8);
                real[i] = (double)s;
                imag[i] = 0.0;
            } else {
                int32_t s = read_le24(chunkBuf + i*frameBytes);
                real[i] = (double)s;
                imag[i] = 0.0;
            }
        }
        fft(real, imag, CHUNK_FRAMES);
        for (int i = 0; i < CHUNK_FRAMES/2; i++)
            bins[i] = sqrt(real[i]*real[i] + imag[i]*imag[i]);
        normalize_bins(bins, CHUNK_FRAMES/2, HEIGHT);
        for (int r = 0; r < HEIGHT; r++) {
            memmove(grid[r], grid[r] + 1, WIDTH - 1);
            grid[r][WIDTH-1] = ' ';
        }
        int num_bins = CHUNK_FRAMES/2;
        for (int r = 0; r < HEIGHT; r++) {
            int bin_idx = r * num_bins / HEIGHT;
            int h       = (int)bins[bin_idx];
            if (h >= HEIGHT) h = HEIGHT - 1;
            int row_idx = (HEIGHT - 1) - h;
            grid[row_idx][WIDTH-1] = 'H';
        }
        printf("\x1b[H");
        for (int r = 0; r < HEIGHT; r++) {
            fputs(grid[r], stdout);
            putchar('\n');
        }
        fflush(stdout);
        usleep(sleepUs);
        framesRead += got;
    }
    printf("\x1b[?25h");  
    printf("\nDone.\n");
    free(chunkBuf);
    fclose(fp);
    return 0;
}