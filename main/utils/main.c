#include "stdlib.h"
#include "stdint.h"

#include "main.h"

uint8_t match(const char* org, const char* new, int osize, int nsize) {
    if(osize != nsize) return 0;
    for(int i = 0; i < osize; i++) 
      if (org[i] != new[i]) return 0;
    return 1;
}

int split(const unsigned char* str, int len, char delim, struct split_result* res) {
    int wnum = 0, tnum = 0;
    char* txt = malloc(0);

    for(int i = 0; i < len; i++) {
        if(str[i] == delim) {
            res[wnum].text = txt;
            res[wnum++].len = tnum;
            tnum = 0;
            txt = malloc(0);
        } else {
            txt = realloc(txt, tnum+1);
            txt[tnum++] = str[i];
        }
    }
    res[wnum].text = txt;
    res[wnum++].len = tnum;

    return wnum;
}