#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "jpeglib.h"
#include "jpegutils.h"
int main(int argc, char **argv) {

    if (argc < 3) {
        printf("usage: ./jpg2yuv inputfile outputfile\n");

        return -1;
    }

    mjpeg_default_handler_verbosity(1);

    FILE *jpgFile = fopen(argv[1], "rb");
    if (jpgFile == NULL) {
        printf("fail to open jpg file: %s\n", argv[1]);
        return -1;
    }
    int w = 640;
    int h = 480;
    char * jpgBuf = (char *) malloc(sizeof(char) * w * h * 2);
    int jpgSize = 0;
    jpgSize = fread(jpgBuf, 1, w * h * 2, jpgFile);
    if (jpgSize <= 0) {
        printf("fail to read jpg file\n");
        return -1;
    }
    fclose(jpgFile);

    char * yuvBuf = (char *) malloc(sizeof(char) * w * h * 3 / 2);
    char *yuvPtr[3] = {yuvBuf, yuvBuf + w * h, yuvBuf + w * h + w * h / 4};
    struct timeval start;
    gettimeofday(&start, NULL);
    decode_jpeg_raw(jpgBuf, jpgSize, 0, 420, w, h , yuvPtr[0], yuvPtr[1], yuvPtr[2]);
    struct timeval end;
    gettimeofday(&end, NULL);
    printf("decode use time: %dms\n", ((end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec) ) / 1000);
    FILE *yuvFile = fopen(argv[2], "wb");
    if (yuvFile == NULL) {
        printf("fail to open yuv file: %s\n", argv[2]);
        return -1;
    }

    fwrite(yuvBuf, 1, w * h * 3 / 2, yuvFile);
    fclose(yuvFile);
    return 0;
}

