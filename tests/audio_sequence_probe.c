#include <stdio.h>
#include <stdlib.h>
#include "alleyway_sfx.h"
int main(int argc,char **argv) {
    if (argc!=4) return 2;
    FILE *fp=fopen(argv[1],"rb"); if (!fp) return 3;
    uint8_t rom[32768]; size_t size=fread(rom,1,sizeof rom,fp); fclose(fp);
    GbApuChannel1 ch; gb_apu_ch1_init(&ch,44100);
    AlleywaySfx s={0};
    for (int frame=0;frame<atoi(argv[3]);frame++) {
        alleyway_sfx_step(&s,&ch,rom,size,frame==0?(uint8_t)atoi(argv[2]):0);
        printf("%u %u %u %u %u %u %u %u %u %u %u %u\n",s.active,s.clock,s.remaining,
               s.lo_up,s.lo_down,s.hi_up,s.hi_down,ch.nr10,ch.nr11,ch.nr12,ch.nr13,ch.nr14);
    }
    return 0;
}
