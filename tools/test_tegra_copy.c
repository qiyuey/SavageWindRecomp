#include "nvn/tegra.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// Independent byte-at-a-time reference, including every byte within a GOB.
static void reference_copy(const uint8_t *src, uint8_t *dst, uint32_t w, uint32_t h,
                           uint32_t d, uint32_t bpb, uint32_t bh, uint32_t bd, bool linear) {
    uint64_t row = (uint64_t)w * bpb, columns = (row + 63) / 64;
    uint64_t rows = (h + (8u << bh) - 1) / (8u << bh);
    for (uint32_t z=0; z<d; z++) for (uint32_t y=0; y<h; y++) for (uint64_t x=0; x<row; x++) {
        uint64_t gob = (((z >> bd) * rows + (y >> (3+bh))) * columns + x/64) * (512ull << (bh+bd));
        gob += (((z & ((1u<<bd)-1)) << bh) + ((y/8) & ((1u<<bh)-1))) * 512;
        uint64_t tile = (x%64/32)*256 + (y%8/2)*64 + (x%32/16)*32 + (y%2)*16 + x%16;
        uint64_t pos = ((uint64_t)z*h+y)*row+x;
        if (linear) dst[pos]=src[gob+tile]; else dst[gob+tile]=src[pos];
    }
}
#define reference_tegra_swizzle(s,o,w,h,d,b,bh,bd) reference_copy(s,o,w,h,d,b,bh,bd,false)
#define reference_tegra_deswizzle(s,o,w,h,d,b,bh,bd) reference_copy(s,o,w,h,d,b,bh,bd,true)
int main(void) {
    unsigned sizes[]={1,2,3,4,8,16}, widths[]={1,3,17,65,513}, heights[]={1,9,65};
    unsigned cases=0;
    for(int si=0;si<6;si++) for(int wi=0;wi<5;wi++) for(int hi=0;hi<3;hi++)
    for(unsigned d=1;d<=3;d+=2) for(unsigned bh=0;bh<4;bh++) for(unsigned bd=0;bd<3;bd++) {
        unsigned bpb=sizes[si],w=widths[wi],h=heights[hi];
        size_t linear=(size_t)w*h*d*bpb;
        size_t tiled=(size_t)((w*bpb+63)/64)*(512u<<(bh+bd))*((h+(8u<<bh)-1)/(8u<<bh))*((d+(1u<<bd)-1)/(1u<<bd));
        uint8_t *src=malloc(linear),*a=malloc(tiled),*b=malloc(tiled),*da=malloc(linear),*db=malloc(linear);
        assert(src&&a&&b&&da&&db);
        for(size_t i=0;i<linear;i++) src[i]=(uint8_t)(i*31+i/97);
        memset(a,0xa5,tiled);memset(b,0xa5,tiled);
        reference_tegra_swizzle(src,a,w,h,d,bpb,bh,bd); tegra_swizzle(src,b,w,h,d,bpb,bh,bd);
        assert(!memcmp(a,b,tiled));
        reference_tegra_deswizzle(a,da,w,h,d,bpb,bh,bd);tegra_deswizzle(b,db,w,h,d,bpb,bh,bd);
        assert(!memcmp(src,da,linear)&&!memcmp(da,db,linear));
        free(src);free(a);free(b);free(da);free(db);cases++;
    }
    printf("PASS: %u layouts match scalar reference, including tails and 3D\n",cases);
}
