#include "nearby_face.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static FR_Context ctx, restored;
static uint8_t frame[96*96*2], blob[FR_DB_BYTES], feat[FR_FEATURE_BYTES];
static void pattern(unsigned seed) {
    unsigned x; uint16_t p;
    for(x=0;x<96*96;x++) {
        seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
        p=(uint16_t)seed; frame[2*x]=(uint8_t)(p>>8);frame[2*x+1]=(uint8_t)p;
    }
}
static int extract(void) {
    FR_ROI r={0,0,96,96};
    return FR_ExtractRGB565(&ctx,frame,sizeof(frame),96,96,192,r,FR_RGB565_MSB_FIRST);
}
int main(void) {
    FR_Config cfg={280,40,3}; FR_Result r; FR_ROI bad={80,0,96,96}; unsigned i;
    FR_Init(&ctx); pattern(123);
    assert(extract()==FR_OK);
    memcpy(feat,ctx.feature,sizeof(feat));
    /* Equivalent RGB565 byte orders must produce identical descriptors. */
    for(i=0;i<sizeof(frame);i+=2){uint8_t t=frame[i];frame[i]=frame[i+1];frame[i+1]=t;}
    {FR_ROI q={0,0,96,96};assert(FR_ExtractRGB565(&ctx,frame,sizeof(frame),96,96,192,q,FR_RGB565_LSB_FIRST)==FR_OK);}
    assert(memcmp(feat,ctx.feature,sizeof(feat))==0);
    r=FR_Predict(&ctx,cfg);assert(r.person_id==-1);
    for(i=0;i<3;i++)assert(FR_Enroll(&ctx,0)==FR_OK);
    r=FR_Predict(&ctx,cfg);assert(r.person_id==0 && r.distance==0);
    /* Equal templates under different identities are ambiguous, not a match. */
    for(i=0;i<3;i++)assert(FR_Enroll(&ctx,1)==FR_OK);
    r=FR_Predict(&ctx,cfg);assert(r.person_id==-1);
    FR_ClearPerson(&ctx,1);
    assert(FR_Export(&ctx,blob,sizeof(blob))==FR_OK);
    FR_Init(&restored);assert(FR_Import(&restored,blob,sizeof(blob))==FR_OK);
    assert(restored.count[0]==3);
    assert(memcmp(ctx.samples,restored.samples,sizeof(ctx.samples))==0);
    blob[100]^=1;assert(FR_Import(&restored,blob,sizeof(blob))==FR_BAD_DATABASE);
    assert(restored.count[0]==3); /* Import is transactional on a corrupt record. */
    pattern(98765);assert(extract()==FR_OK);cfg.max_distance=0;
    r=FR_Predict(&ctx,cfg);assert(r.person_id==-1 && r.distance>0);
    assert(FR_ExtractRGB565(&ctx,frame,sizeof(frame),96,96,192,bad,FR_RGB565_MSB_FIRST)==FR_BAD_ARGUMENT);
    assert(FR_Enroll(&ctx,0)==FR_NO_FEATURE); /* Never enroll a stale frame. */
    memset(frame,0,sizeof(frame));assert(extract()==FR_BAD_IMAGE);
    assert(FR_Enroll(&ctx,FR_MAX_PEOPLE)==FR_BAD_ARGUMENT);
    pattern(123);assert(extract()==FR_OK);
    assert(FR_Enroll(&ctx,0)==FR_OK);assert(FR_Enroll(&ctx,0)==FR_OK);
    assert(FR_Enroll(&ctx,0)==FR_PERSON_FULL);
    cfg.max_distance=1001;assert(FR_Predict(&ctx,cfg).status==FR_BAD_ARGUMENT);
    printf("PASS: byte order, matching, ambiguity, unknown, capacity, quality, bounds, persistence CRC.\n");
    printf("FR_Context=%zu bytes; database record=%u bytes\n",sizeof(ctx),(unsigned)FR_DB_BYTES);
    return 0;
}
