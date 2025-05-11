```
 _____  _____  _____ 
|  ___||  ___||_   _|
| |_   | |_     | |    Created By: Max Warren
|_|    |_|      |_|
```

A compact C program to render WAV audio as a live ASCII Frequency STFT directly from your terminal

## Requirements
- GCC or Clang
- ANSI-compatible terminal

## Build & Run
```bash
gcc -O2 -o fft fft.c -lm

./fft 1.wav