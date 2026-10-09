#ifndef FACE_APP_HAL_H
#define FACE_APP_HAL_H
#include "stm32h7xx_hal.h"
#include "nearby_face.h"
void FaceApp_Init(DCMI_HandleTypeDef *camera,UART_HandleTypeDef *uart);
void FaceApp_Poll(void);
/* Call from main context only, e.g. your SD/Flash save/load command. */
FR_Context *FaceApp_Database(void);
#endif
