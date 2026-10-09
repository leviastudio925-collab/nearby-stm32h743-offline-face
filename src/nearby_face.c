#include "nearby_face.h"
#include <string.h>

void FR_Init(FR_Context *c) {
    unsigned v,k,next=0;
    if(!c)return;
    memset(c,0,sizeof(*c));
    for(v=0;v<256;v++) {
        unsigned changes=0;
        for(k=0;k<8;k++)changes+=((v>>k)^(v>>((k+1u)&7u)))&1u;
        c->uniform_lut[v]=(uint8_t)(changes<=2u?next++:58u);
    }
}
static uint8_t luminance(const uint8_t *p,FR_ByteOrder order) {
    unsigned v=order==FR_RGB565_MSB_FIRST?((unsigned)p[0]<<8)|p[1]:((unsigned)p[1]<<8)|p[0];
    unsigned r=((v>>11)&31u)*255u/31u,g=((v>>5)&63u)*255u/63u,b=(v&31u)*255u/31u;
    return (uint8_t)((77u*r+150u*g+29u*b)>>8);
}
int FR_ExtractRGB565(FR_Context *c,const uint8_t *frame,size_t bytes,
                    uint16_t width,uint16_t height,size_t stride,
                    FR_ROI roi,FR_ByteOrder order) {
    unsigned x,y,cx,cy; uint32_t sum=0,sq=0,gradient=0;
    if(!c)return FR_BAD_ARGUMENT;
    c->feature_valid=0;
    if(!frame||!width||!height||stride<(size_t)width*2u||
       stride>SIZE_MAX/(size_t)height||bytes<stride*(size_t)height||
       roi.width<FR_IMAGE_SIZE||roi.height<FR_IMAGE_SIZE||
       (uint32_t)roi.x+roi.width>width||(uint32_t)roi.y+roi.height>height||
       (order!=FR_RGB565_MSB_FIRST&&order!=FR_RGB565_LSB_FIRST))return FR_BAD_ARGUMENT;
    /* Average a small 2x2 area when reducing a larger ROI to 96x96. */
    for(y=0;y<FR_IMAGE_SIZE;y++)for(x=0;x<FR_IMAGE_SIZE;x++) {
        unsigned sx=roi.x+(2u*x+1u)*roi.width/(2u*FR_IMAGE_SIZE);
        unsigned sy=roi.y+(2u*y+1u)*roi.height/(2u*FR_IMAGE_SIZE);
        unsigned ex=sx+1u<(unsigned)roi.x+roi.width?sx+1u:sx;
        unsigned ey=sy+1u<(unsigned)roi.y+roi.height?sy+1u:sy;
        unsigned g=luminance(frame+sy*stride+2u*sx,order);
        if(roi.width>FR_IMAGE_SIZE&&roi.height>FR_IMAGE_SIZE)
            g=(g+luminance(frame+sy*stride+2u*ex,order)+luminance(frame+ey*stride+2u*sx,order)+luminance(frame+ey*stride+2u*ex,order))/4u;
        c->gray[y*FR_IMAGE_SIZE+x]=(uint8_t)g;sum+=g;sq+=g*g;
    }
    for(y=1;y<FR_IMAGE_SIZE;y++)for(x=1;x<FR_IMAGE_SIZE;x++) {
        int a=c->gray[y*FR_IMAGE_SIZE+x],b=c->gray[y*FR_IMAGE_SIZE+x-1u];
        int d=c->gray[(y-1u)*FR_IMAGE_SIZE+x];
        gradient+=(uint32_t)(a>b?a-b:b-a)+(uint32_t)(a>d?a-d:d-a);
    }
    {unsigned mean=sum/(FR_IMAGE_SIZE*FR_IMAGE_SIZE);
     unsigned variance=sq/(FR_IMAGE_SIZE*FR_IMAGE_SIZE)-mean*mean;
     if(mean<20u||mean>235u||variance<100u||gradient<3u*2u*95u*95u)return FR_BAD_IMAGE;}
    for(cy=0;cy<FR_GRID;cy++)for(cx=0;cx<FR_GRID;cx++) {
        uint16_t hist[FR_BINS]={0};unsigned total=0,k,largest=0,norm_sum=0;
        uint8_t *dst=c->feature+(cy*FR_GRID+cx)*FR_BINS;
        for(y=cy*12u;y<(cy+1u)*12u;y++)for(x=cx*12u;x<(cx+1u)*12u;x++) {
            unsigned center,code=0;const uint8_t *p;
            if(x==0u||y==0u||x==95u||y==95u)continue;
            p=&c->gray[y*96u+x];center=*p;
            code|=(unsigned)(p[-97]>=center)<<0;code|=(unsigned)(p[-96]>=center)<<1;
            code|=(unsigned)(p[-95]>=center)<<2;code|=(unsigned)(p[1]>=center)<<3;
            code|=(unsigned)(p[97]>=center)<<4;code|=(unsigned)(p[96]>=center)<<5;
            code|=(unsigned)(p[95]>=center)<<6;code|=(unsigned)(p[-1]>=center)<<7;
            hist[c->uniform_lut[code]]++;total++;
        }
        for(k=0;k<FR_BINS;k++){if(hist[k]>hist[largest])largest=k;dst[k]=(uint8_t)(hist[k]*255u/total);norm_sum+=dst[k];}
        dst[largest]=(uint8_t)(dst[largest]+255u-norm_sum);
    }
    c->feature_valid=1;return FR_OK;
}
int FR_Enroll(FR_Context *c,unsigned person) {
    if(!c||person>=FR_MAX_PEOPLE)return FR_BAD_ARGUMENT;
    if(!c->feature_valid)return FR_NO_FEATURE;
    if(c->count[person]>=FR_SAMPLES_PER_PERSON)return FR_PERSON_FULL;
    memcpy(c->samples[person][c->count[person]],c->feature,FR_FEATURE_BYTES);
    c->count[person]++;return FR_OK;
}
void FR_ClearPerson(FR_Context *c,unsigned person) {
    if(c&&person<FR_MAX_PEOPLE){c->count[person]=0;memset(c->samples[person],0,sizeof(c->samples[person]));}
}
static uint16_t distance(const uint8_t *a,const uint8_t *b) {
    uint32_t sum=0;unsigned i;
    for(i=0;i<FR_FEATURE_BYTES;i++)sum+=(uint32_t)(a[i]>b[i]?a[i]-b[i]:b[i]-a[i]);
    return (uint16_t)(sum*1000u/(FR_GRID*FR_GRID*510u));
}
FR_Result FR_Predict(const FR_Context *c,FR_Config cfg) {
    FR_Result r={FR_UNKNOWN,-1,1000,1000};unsigned p,s;int candidate=-1;
    if(!c||cfg.max_distance>1000u||cfg.min_margin>1000u||!cfg.min_samples||cfg.min_samples>FR_SAMPLES_PER_PERSON){r.status=FR_BAD_ARGUMENT;return r;}
    if(!c->feature_valid){r.status=FR_NO_FEATURE;return r;}
    for(p=0;p<FR_MAX_PEOPLE;p++) {
        uint16_t d=1000;
        if(c->count[p]<cfg.min_samples||c->count[p]>FR_SAMPLES_PER_PERSON)continue;
        for(s=0;s<c->count[p];s++){uint16_t v=distance(c->feature,c->samples[p][s]);if(v<d)d=v;}
        if(d<r.distance){r.second_distance=r.distance;r.distance=d;candidate=(int)p;}
        else if(d<r.second_distance)r.second_distance=d;
    }
    if(candidate>=0&&r.distance<=cfg.max_distance&&r.second_distance>r.distance&&
       (unsigned)(r.second_distance-r.distance)>=cfg.min_margin){r.status=FR_OK;r.person_id=candidate;}
    return r;
}
static uint32_t crc32(const uint8_t *p,size_t n) {
    uint32_t crc=0xffffffffu;unsigned k;
    while(n--){crc^=*p++;for(k=0;k<8;k++)crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);}
    return ~crc;
}
static const uint8_t header[12]={'N','F','R','1',1,FR_MAX_PEOPLE,FR_SAMPLES_PER_PERSON,FR_GRID,FR_BINS,FR_IMAGE_SIZE,0,0};
int FR_Export(const FR_Context *c,uint8_t *out,size_t bytes) {
    uint32_t crc;unsigned i;
    if(!c||!out||bytes<FR_DB_BYTES)return FR_BAD_ARGUMENT;
    for(i=0;i<FR_MAX_PEOPLE;i++)if(c->count[i]>FR_SAMPLES_PER_PERSON)return FR_BAD_DATABASE;
    memcpy(out,header,12);memcpy(out+16,c->count,FR_MAX_PEOPLE);
    memcpy(out+16+FR_MAX_PEOPLE,c->samples,sizeof(c->samples));
    crc=crc32(out+16,FR_DB_BYTES-16u);for(i=0;i<4;i++)out[12+i]=(uint8_t)(crc>>(i*8u));
    return FR_OK;
}
int FR_Import(FR_Context *c,const uint8_t *data,size_t bytes) {
    uint32_t crc=0;unsigned i;
    if(!c||!data)return FR_BAD_ARGUMENT;
    if(bytes!=FR_DB_BYTES||memcmp(data,header,12))return FR_BAD_DATABASE;
    for(i=0;i<4;i++)crc|=(uint32_t)data[12+i]<<(i*8u);
    if(crc!=crc32(data+16,FR_DB_BYTES-16u))return FR_BAD_DATABASE;
    for(i=0;i<FR_MAX_PEOPLE;i++)if(data[16+i]>FR_SAMPLES_PER_PERSON)return FR_BAD_DATABASE;
    memcpy(c->count,data+16,FR_MAX_PEOPLE);memcpy(c->samples,data+16+FR_MAX_PEOPLE,sizeof(c->samples));
    c->feature_valid=0;return FR_OK;
}
