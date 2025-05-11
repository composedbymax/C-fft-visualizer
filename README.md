```
 _____  _____  _____ 
|  ___||  ___||_   _|
| |_   | |_     | |    Created By: Max Warren
|_|    |_|      |_|
```

A compact C program to render WAV audio as a live ASCII frequency spectrum directly from your terminal

## Requirements
- GCC or Clang
- ANSI-compatible terminal

## Build & Run
```bash
gcc -O2 -o -lm fft fft.c

./fft 1.wav