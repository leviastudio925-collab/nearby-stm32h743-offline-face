/* Integration example, requires CubeMX-generated HAL, DCMI/DMA/IRQ setup,
   and an OV2640 sensor driver configured for uncompressed QVGA RGB565.
   GCC / Armclang. See README for the REQUIRED linker sections. */
#include "face_app_hal.h"
#include <stdio.h>
#include <string.h>
#define CAM_W 320u
#define CAM_H 240u
#define CAM_BYTES (CAM_W*CAM_H*2u)
#define CAPTURE_TIMEOUT_MS 1500u

__attribute__((section(".camera_dma"),aligned(32)))
static uint8_t camera_frame[CAM_BYTES];
__attribute__((section(".face_work"),aligned(32)))
static FR_Context face;
static DCMI_HandleTypeDef *dcmi;
static UART_HandleTypeDef *serial;
static volatile uint8_t frame_done,frame_error;
/* Change this ROI to the rectangle shown on your own LCD preview. */
static const FR_ROI roi={96,56,128,128};
/* Starting values only; calibrate with unseen captures before using them. */
static const FR_Config config={280,40,3};
static void say(const char *s) {
    (void)HAL_UART_Transmit(serial,(uint8_t *)s,(uint16_t)strlen(s),500);
}
void FaceApp_Init(DCMI_HandleTypeDef *camera,UART_HandleTypeDef *uart) {
    dcmi=camera;serial=uart;FR_Init(&face);
    if(!camera||!uart)return;
    say("NEARBY offline aligned-face prototype\r\n"
        "1..5: enroll one NEW capture for person 1..5 (3..5 samples each)\r\n"
        "r: capture + identify; a..e: delete person 1..5; ?: help\r\n"
        "Align face in x=96,y=56,w=128,h=128. Templates are RAM-only.\r\n");
}
/* If your project already defines these callbacks, merge the assignments
   below into those callbacks instead of defining each symbol twice. */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *h) {
    if(h==dcmi)frame_done=1;
}
void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *h) {
    if(h==dcmi)frame_error=1;
}
static int capture(void) {
    uint32_t started;
    frame_done=0;frame_error=0;
    /* camera_frame is exclusively owned by DMA until capture is stopped.
       Both its address and its allocation size are multiples of 32. */
    if((SCB->CCR&SCB_CCR_DC_Msk)!=0u)
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)camera_frame,(int32_t)CAM_BYTES);
    __DSB();
    /* HAL DCMI Length is a count of 32-bit words, not pixels or bytes. */
    if(HAL_DCMI_Start_DMA(dcmi,DCMI_MODE_SNAPSHOT,(uint32_t)(uintptr_t)camera_frame,CAM_BYTES/4u)!=HAL_OK) {
        (void)HAL_DCMI_Stop(dcmi);return -1;
    }
    started=HAL_GetTick();
    while(!frame_done&&!frame_error) {
        if((uint32_t)(HAL_GetTick()-started)>=CAPTURE_TIMEOUT_MS) {
            (void)HAL_DCMI_Stop(dcmi);return -2;
        }
    }
    if(HAL_DCMI_Stop(dcmi)!=HAL_OK||frame_error)return -3;
    __DSB();
    if((SCB->CCR&SCB_CCR_DC_Msk)!=0u)
        SCB_InvalidateDCache_by_Addr((uint32_t *)camera_frame,(int32_t)CAM_BYTES);
    __DSB();return 0;
}
void FaceApp_Poll(void) {
    uint8_t key;int status;char msg[128];
    if(!serial||!dcmi)return;
    if(HAL_UART_Receive(serial,&key,1,1)!=HAL_OK)return;
    if(key=='?'){say("1..5 enroll; r identify; a..e delete. Face must be aligned.\r\n");return;}
    if(key>='a'&&key<='e') {
        FR_ClearPerson(&face,(unsigned)(key-'a'));say("Deleted selected person.\r\n");return;
    }
    if(key!='r'&&(key<'1'||key>'5'))return;
    status=capture();
    if(status){snprintf(msg,sizeof(msg),"Capture failed (%d); check camera, DMA, IRQ and clocks.\r\n",status);say(msg);return;}
    status=FR_ExtractRGB565(&face,camera_frame,sizeof(camera_frame),CAM_W,CAM_H,CAM_W*2u,roi,FR_RGB565_MSB_FIRST);
    if(status!=FR_OK){snprintf(msg,sizeof(msg),"Image rejected (%d); check light, focus and RGB565 order.\r\n",status);say(msg);return;}
    if(key>='1'&&key<='5') {
        unsigned person=(unsigned)(key-'1');status=FR_Enroll(&face,person);
        snprintf(msg,sizeof(msg),"ENROLL person=%u samples=%u/5 status=%d\r\n",person+1u,(unsigned)face.count[person],status);
    } else {
        FR_Result r=FR_Predict(&face,config);
        if(r.person_id>=0)
            snprintf(msg,sizeof(msg),"MATCH candidate=%d distance=%u second=%u\r\n",r.person_id+1,(unsigned)r.distance,(unsigned)r.second_distance);
        else snprintf(msg,sizeof(msg),"UNKNOWN distance=%u second=%u status=%d\r\n",(unsigned)r.distance,(unsigned)r.second_distance,r.status);
    }
    say(msg);
}
FR_Context *FaceApp_Database(void){return &face;}
