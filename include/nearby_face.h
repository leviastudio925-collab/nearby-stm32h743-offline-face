#ifndef NEARBY_FACE_H
#define NEARBY_FACE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Fixed, manually aligned face ROI. This library does NOT detect faces. */
#define FR_IMAGE_SIZE 96u
#define FR_GRID 8u
#define FR_BINS 59u
#define FR_FEATURE_BYTES (FR_GRID*FR_GRID*FR_BINS)
#define FR_MAX_PEOPLE 5u
#define FR_SAMPLES_PER_PERSON 5u
#define FR_DB_BYTES (16u+FR_MAX_PEOPLE+FR_MAX_PEOPLE*FR_SAMPLES_PER_PERSON*FR_FEATURE_BYTES)
enum {FR_OK=0,FR_BAD_ARGUMENT=-1,FR_BAD_IMAGE=-2,FR_NO_FEATURE=-3,
      FR_PERSON_FULL=-4,FR_BAD_DATABASE=-5,FR_UNKNOWN=-6};
typedef enum {FR_RGB565_MSB_FIRST=0,FR_RGB565_LSB_FIRST=1} FR_ByteOrder;
typedef struct {uint16_t x,y,width,height;} FR_ROI;
typedef struct {
    /* Distance is 0..1000, lower is better; NOT a confidence percentage.
       Calibrate these two thresholds on held-out photos from your camera. */
    uint16_t max_distance, min_margin;
    uint8_t min_samples;
} FR_Config;
typedef struct {
    int status, person_id; /* person_id=-1 means unknown / invalid */
    uint16_t distance, second_distance;
} FR_Result;
typedef struct {
    uint8_t count[FR_MAX_PEOPLE];
    uint8_t samples[FR_MAX_PEOPLE][FR_SAMPLES_PER_PERSON][FR_FEATURE_BYTES];
    uint8_t feature[FR_FEATURE_BYTES];
    uint8_t gray[FR_IMAGE_SIZE*FR_IMAGE_SIZE];
    uint8_t uniform_lut[256];
    uint8_t feature_valid;
} FR_Context;
void FR_Init(FR_Context *ctx);
int FR_ExtractRGB565(FR_Context *ctx,const uint8_t *frame,size_t bytes,
                    uint16_t width,uint16_t height,size_t stride,
                    FR_ROI roi,FR_ByteOrder order);
int FR_Enroll(FR_Context *ctx,unsigned person);
void FR_ClearPerson(FR_Context *ctx,unsigned person);
FR_Result FR_Predict(const FR_Context *ctx,FR_Config config);
int FR_Export(const FR_Context *ctx,uint8_t *out,size_t bytes);
int FR_Import(FR_Context *ctx,const uint8_t *data,size_t bytes);
#ifdef __cplusplus
}
#endif
#endif
