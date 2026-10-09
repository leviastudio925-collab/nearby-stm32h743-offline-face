# NEARBY：STM32H743 + OV2640 本地家人识别原型

本包提供可以编译的纯 C 识别核心，以及 STM32 HAL 接入示例。**它是固定取景框内的人脸模板比对版本，不含自动找脸、关键点对齐、神经网络或活体检测。** 使用时需让人脸对准同一个方框，距离、角度和光照尽量一致。无需联网、API Key 或云端模型。

目前验证了 PC 上的算法单元测试；**未在你的开发板上烧录验证，也没有实测识别率或帧率。** HAL 示例需要你用具体开发板的 CubeMX 工程、引脚配置和 OV2640 驱动接入后才能运行，不是开箱即烧录的完整工程。

## 文件

| 文件 | 作用 |
|---|---|
| `include/nearby_face.h` | 数据结构、参数和 API |
| `src/nearby_face.c` | RGB565 转灰度、Uniform LBP 特征、录入与身份比对 |
| `examples/face_app_hal.c/.h` | DCMI 单帧采集、缓存维护、串口命令示例 |
| `examples/linker_sections.ld.txt` | DMA 与算法内存的链接段配置 |
| `tests/test_face.c` | 字节序、匹配、拒识、边界和数据库校验测试 |

## 1. 在 CubeMX / STM32CubeIDE 中建工程

1. 按开发板**完整型号**选择 MCU，例如 H743 后面的封装与 Flash 容量后缀，不要仅凭 H743 随便选引脚。
2. 配置时钟、调试口、一个串口（例如 115200、8N1）和 SCCB 所用的 I2C。
3. 配置摄像头的 D0–D7、PCLK、HREF、VSYNC 到 DCMI；XCLK 由适合板子接线的定时器或 MCO 输出。RESET/PWDN 按模块手册控制。数据电平、电源和是否带稳压器以实际摄像头模块为准。
4. DCMI 设为 8-bit、hardware synchronization、每帧采样、JPEG disabled。PCLK 采样沿、HREF/VSYNC 极性应匹配摄像头寄存器设置，不能脱离模块/驱动随意固定。
5. 配置 DCMI 的 DMA 请求：外设到内存、32-bit 外设/内存宽度、内存递增、normal 模式；启用 DMA 和 DCMI 中断，确保各 IRQ 调用对应的 HAL handler。
6. 集成开发板厂家提供的 OV2640 驱动，或移植 [ST 的 OV2640 BSP 组件](https://github.com/STMicroelectronics/stm32-ov2640)。该组件仍需你实现板级 I2C、时钟、延时和复位接入；本包未复制第三方驱动。
7. 摄像头配置为 **320×240、RGB565、非 JPEG**。先单独验证彩条/实际画面方向、颜色和每帧 153600 字节，再接入识别。

## 2. 加入本包源码并配置内存

- 将 `nearby_face.c`、`face_app_hal.c` 加入编译，添加 `include`、`examples` 到头文件搜索路径。
- 将 `linker_sections.ld.txt` 中的两个段加入 `.ld` 的 `SECTIONS` 中。确认 `RAM_D1` 是 `0x24000000` 开始的 AXI SRAM。
- 摄像头帧缓存必须 32-byte 对齐，并放在 DMA 可访问的 SRAM。**H743 的 DMA1/2 不能访问 DTCM**，因此不要将帧缓存放在 `0x20000000`。示例已经在启动 DMA 前、完成后做 D-Cache 维护。这一点可对照 [ST 的 H743 DMA 示例说明](https://github.com/STMicroelectronics/STM32CubeH7/blob/master/Projects/STM32H743I-EVAL/Examples/DMA/DMA_FIFOMode/readme.txt) 和 [AN4839](https://www.st.com/resource/en/application_note/DM00272913-.pdf)。
- `HAL_DCMI_Start_DMA()` 的 Length 是 **32-bit word 数量**；QVGA RGB565 是 `320*240*2/4 = 38400`。对应实现见 [ST HAL DCMI 驱动](https://github.com/STMicroelectronics/stm32h7xx-hal-driver/blob/master/Src/stm32h7xx_hal_dcmi.c)。
- 示例面向 GCC / Armclang。Keil 用户需在 scatter 文件中设置等价的 AXI SRAM 区域。

算法上下文为 **107654 字节**，单帧缓存为 **153600 字节**，合计约 **255 KiB**，另外为 HAL、栈、显示缓存等预留空间。本库不使用堆分配。导出数据库时还需额外 **94421 字节**缓冲；不要将上下文或导出缓冲定义为函数内的大型栈变量。

## 3. 主循环接入

在 CubeMX 已生成外设初始化、**OV2640 已配置好**之后调用：

```c
#include "face_app_hal.h"

/* 以下放在 main() 中，MX_* 初始化及摄像头初始化之后。 */
FaceApp_Init(&hdcmi, &huart3); /* 改为你实际使用的串口句柄 */
while (1) {
    FaceApp_Poll();
}
```

`face_app_hal.c` 已定义 DCMI 完成与错误回调。如果其他文件已有同名回调，应合并回调内容，不要重复定义。中断里仅设置标志；特征提取和比对在主循环执行。每次指令都会重新采集一帧，发生超时/错误时不使用旧帧。

## 4. 录入家人和离线识别

默认取景框为 QVGA 中的 `x=96, y=56, width=128, height=128`。在 LCD 预览上画出同一个方框，或先把采集帧发到电脑确认构图；示例本身不含 LCD 驱动。

1. 让第一位家人正对摄像头，把脸居中放入方框，眼睛高度保持一致。
2. 串口发 `1`，采集一张。稍微改变表情/角度，再分别发送 `1`，录入 3–5 张。**不要用同一张静态照片重复凑够样本数。**
3. 第二位发 `2`，以此类推到 `5`。每个数字只增加该人的一个样本，不会自动覆盖满库样本。
4. 发 `r`，采集新画面并比对。返回示例：

```text
ENROLL person=1 samples=3/5 status=0
MATCH candidate=1 distance=123 second=278
UNKNOWN distance=367 second=402 status=-6
```

这里数值仅展示输出格式，不是实测结果。`distance` 越小表示模板越接近，**不是置信度百分比**。`candidate=1` 可在你的上层应用映射为“爸爸”等名字。发送 `a`–`e` 分别删除 1–5 号，再重新录入。

如果颜色明显错误，先检查摄像头实际输出是否 RGB565；再核对 `FR_RGB565_MSB_FIRST` / `FR_RGB565_LSB_FIRST`。不要拿 YUV/JPEG 数据直接当作 RGB565 处理。

## 5. 阈值需要用真实人脸标定

示例参数 `{280, 40, 3}` 分别是最大距离、第一/第二候选人的最小间隔、每人最少样本数。**280 和 40 只是起始值，未经真实人脸数据标定。**

- 每位家人另采未参与录入的新照片，记录同人距离；另拍其他人、空场景和常见物品，记录误匹配情况。
- 选取能分开同人和陌生人的距离阈值，并检查相似候选人的差值。
- 若两类距离明显重叠，应改善对齐/光照或换用带人脸检测和特征模型的方案，不能靠放宽阈值宣称识别成功。
- 本版本只有曝光、纹理质量检查，**没有确认画面中一定存在人脸**；纹理物体、照片或手机屏幕都可能被误匹配。不要直接用它控制门锁或装置运动。

实现使用 96×96 灰度图、8×8 分区、59-bin Uniform LBP、量化直方图和 L1 距离。思路可参考 [OpenCV 的 LBPH 文档](https://docs.opencv.org/4.13.0/df/d25/classcv_1_1face_1_1LBPHFaceRecognizer.html)，但这里是为 MCU 写的独立精简实现，**不是 OpenCV 的逐位兼容实现**，不能直接导入 OpenCV 模型或沿用其距离阈值。

## 6. 断电保存

HAL 示例默认把家人模板存在 RAM 中，重启会清空。已实现 `FR_Export` / `FR_Import`，使用版本头和 CRC32 检查数据库：

```c
static uint8_t db_blob[FR_DB_BYTES]; /* 分配在有空间的 SRAM，勿放栈上 */

/* 保存：在 main 上下文调用；成功后由你的存储驱动写入整个记录。 */
int status = FR_Export(FaceApp_Database(), db_blob, sizeof(db_blob));

/* 加载：先由存储驱动读出完整记录，再在 FaceApp_Init 之后调用。 */
status = FR_Import(FaceApp_Database(), db_blob, sizeof(db_blob));
```

可接 SD 卡、外部 Flash，或确实在链接文件中保留的片内 Flash 区域。本包没有擅自固定 Flash 擦除地址，因为还不知道你的完整芯片型号及固件分区。CRC 用于检测损坏，不提供加密；存储失败要保留上一份有效数据，不要每帧写 Flash。

## 7. 已完成的验证

在 Windows MSVC 以 C11、`/W4 /WX` 编译算法核心并运行测试，覆盖 RGB565 字节序一致性、已录入模板匹配、不同身份同模板的歧义拒绝、阈值拒识、样本容量、坏图/越界、旧帧失效、数据库导出/导入和 CRC 损坏拒绝。

这些使用合成图案验证代码行为，**不等于人脸准确率测试**。摄像头电气连接、DMA 中断、真实人脸识别效果以及 ARM 上性能，需要在实际开发板完成验证。

本机测试：`tests\run_tests.cmd`。也可在任意有 C 编译器的电脑用 CMake 构建核心测试；HAL 接入示例由 STM32 工程编译，不包含在 PC 测试目标中。
