#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#define WIDTH        80
#define HEIGHT       20
#define FFT_SIZE     1024
#define HOP_SIZE     (FFT_SIZE/2)
static const char *PALETTE = " .:-=+*#%@";
double window[FFT_SIZE];
static char grid[HEIGHT][WIDTH+1];
static uint16_t read_le16(const uint8_t *b) { return b[0] | (b[1] << 8); }
static uint32_t read_le32(const uint8_t *b) { return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24); }
static int32_t  read_le24(const uint8_t *b) { int32_t v = b[0] | (b[1]<<8) | (b[2]<<16); if (v & 0x800000) v |= ~0xFFFFFF; return v; }
void fft(double *real, double *imag, int n) {
    if (n <= 1) return;
    double even_r[n/2], even_i[n/2], odd_r[n/2], odd_i[n/2];
    for (int i = 0; i < n/2; i++) {
        even_r[i] = real[2*i];    even_i[i] = imag[2*i];
        odd_r[i]  = real[2*i+1];  odd_i[i]  = imag[2*i+1];
    }
    fft(even_r, even_i, n/2);
    fft(odd_r,  odd_i,  n/2);
    for (int i = 0; i < n/2; i++) {
        double angle = 2*M_PI*i/n;
        double c = cos(angle), s = sin(angle);
        double tr =  c*odd_r[i] + s*odd_i[i];
        double ti =  c*odd_i[i] - s*odd_r[i];
        real[i]     = even_r[i] + tr;
        imag[i]     = even_i[i] + ti;
        real[i+n/2] = even_r[i] - tr;
        imag[i+n/2] = even_i[i] - ti;
    }
}
void make_hann_window(void) {
    for (int n = 0; n < FFT_SIZE; n++)
        window[n] = 0.5 * (1.0 - cos(2*M_PI*n/(FFT_SIZE-1)));
}
void init_grid(void) {
    for (int r = 0; r < HEIGHT; r++) {
        memset(grid[r], ' ', WIDTH);
        grid[r][WIDTH] = '\0';
    }
}
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.wav>\n", argv[0]);
        return 1;
    }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror("fopen"); return 1; }
    char riff[4], wave[4];
    fread(riff,1,4,fp);
    fseek(fp,4,SEEK_CUR);
    fread(wave,1,4,fp);
    if (memcmp(riff,"RIFF",4) || memcmp(wave,"WAVE",4)) {
        fprintf(stderr,"Not a valid WAVE file\n"); fclose(fp);
        return 1;
    }
    uint16_t audioFormat=0, numCh=0, bits=0;
    uint32_t sampleRate=0, dataBytes=0;
    char chunkID[5] = {0}; uint8_t sizeBuf[4];
    while (fread(chunkID,1,4,fp)==4) {
        fread(sizeBuf,4,1,fp);
        uint32_t chunkSize = read_le32(sizeBuf);
        if (!memcmp(chunkID,"fmt ",4)) {
            uint8_t fmtbuf[16]; fread(fmtbuf,1,16,fp);
            audioFormat = read_le16(fmtbuf);
            numCh       = read_le16(fmtbuf+2);
            sampleRate  = read_le32(fmtbuf+4);
            bits        = read_le16(fmtbuf+14);
            fseek(fp, chunkSize-16, SEEK_CUR);
        } else if (!memcmp(chunkID,"data",4)) {
            dataBytes = chunkSize; break;
        } else {
            fseek(fp, chunkSize, SEEK_CUR);
        }
    }
    if (audioFormat!=1 || (bits!=8 && bits!=16 && bits!=24)) {
        fprintf(stderr,"Unsupported format PCM=%d bits=%d\n",audioFormat,bits);
        fclose(fp); return 1;
    }
    uint32_t bytesPerSample = bits/8;
    uint32_t frameBytes     = bytesPerSample * numCh;
    size_t   totalFrames    = dataBytes / frameBytes;
    uint8_t  circular[FFT_SIZE * frameBytes];
    double   real[FFT_SIZE], imag[FFT_SIZE], mags[FFT_SIZE/2];
    memset(circular, 0, sizeof(circular));
    size_t buffill = 0;
    make_hann_window();
    init_grid();
    printf("\x1b[2J");
    printf("\x1b[?25l");
    size_t framesRead = 0;
    while (framesRead < totalFrames) {
        size_t toRead = HOP_SIZE;
        size_t got = fread(circular + buffill, frameBytes, toRead, fp);
        if (!got) break;
        buffill += got * frameBytes;
        framesRead += got;
        if (buffill < FFT_SIZE * frameBytes) continue;
        for (int i = 0; i < FFT_SIZE; i++) {
            double sample = 0.0;
            for (int ch = 0; ch < numCh; ch++) {
                uint8_t *ptr = circular + (i*frameBytes + ch*bytesPerSample);
                int32_t  s = (bits==8)
                    ? (ptr[0] - 128)
                    : (bits==16)
                        ? (int16_t)(ptr[0] | (ptr[1]<<8))
                        : read_le24(ptr);
                sample += (double)s;
            }
            real[i] = (sample / numCh) * window[i];
            imag[i] = 0.0;
        }
        fft(real, imag, FFT_SIZE);
        double maxm = 1e-12;
        for (int i = 0; i < FFT_SIZE/2; i++) {
            mags[i] = sqrt(real[i]*real[i] + imag[i]*imag[i]);
            if (mags[i] > maxm) maxm = mags[i];
        }
        for (int r = 0; r < HEIGHT; r++) {
            memmove(grid[r], grid[r]+1, WIDTH-1);
            grid[r][WIDTH-1] = ' ';
        }
        for (int i = 0; i < FFT_SIZE/2; i++) {
            double freq_bin = (double)i;
            double lf = log10(1 + 9 * freq_bin/(FFT_SIZE/2));
            int row = HEIGHT - 1 - (int)(lf * (HEIGHT-1));
            if (row < 0) row = 0;
            if (row >= HEIGHT) row = HEIGHT-1;
            double norm = mags[i] / maxm;
            int idx = (int)(norm * (strlen(PALETTE)-1));
            if (idx < 0) idx = 0;
            if (idx >= (int)strlen(PALETTE)) idx = strlen(PALETTE)-1;
            grid[row][WIDTH-1] = PALETTE[idx];
        }
        printf("\x1b[H");
        for (int r = 0; r < HEIGHT; r++) puts(grid[r]);
        fflush(stdout);
        memmove(circular, circular + HOP_SIZE*frameBytes,
                buffill - HOP_SIZE*frameBytes);
        buffill -= HOP_SIZE*frameBytes;
        usleep((useconds_t)(HOP_SIZE * 1e6 / sampleRate));
    }
    printf("\x1b[?25h\nDone.\n");
    fclose(fp);
    return 0;
}