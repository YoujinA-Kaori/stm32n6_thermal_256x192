# IMX219 摄像头驱动 + 自适应（NECCS / STM32N647）

从 NECCS 工程抽出的 IMX219 sensor 驱动，外加白平衡/自动曝光等自适应参考代码，
可直接给同学移植参考。

## 文件

| 文件 | 说明 |
|------|------|
| `Inc/app_camera_imx219.h` | sensor 驱动对外 API |
| `Src/app_camera_imx219.c` | 寄存器配置、上电、I2C 读写、曝光/增益、诊断 |
| `Inc/app_camera_isp.h` | 自适应（AE/AWB/ISP 色调链）对外 API |
| `Src/app_camera_isp.c` | 自动曝光、灰世界白平衡、黑电平/WB/gamma 配置、帧统计 |
| `Appli/Core/Inc/i2c.h` | 工程现有 I2C4 共享总线互斥接口 |

## 当前模式

- 分辨率：640×480，15 fps
- 输出：CSI-2 RAW10，2-lane（`CSI_LANE_MODE=0x01`）
- 输入时钟：24 MHz；I2C 7-bit 地址 `0x10`；Chip ID `0x0219`

## 一、sensor 驱动 API（`app_camera_imx219.h`）

```c
AppCameraIMX219Config_t cfg = {
  .width = 640, .height = 480, .fps = 15, .input_clock_hz = 24000000UL,
};
uint16_t chip_id = 0;
AppCameraIMX219_Init(&cfg, &chip_id);        // 上电 + 复位 + 全部寄存器表
AppCameraIMX219_SetStream(1);                // 开流
AppCameraIMX219_SetTestPattern(1);           // 彩条测试图
AppCameraIMX219_SetExposure(0x0D00, 0x80, 0x0100); // 曝光行/模拟增益code/数字增益
AppCameraIMX219_UpdateDiagnostics();
```

## 二、自适应 API（`app_camera_isp.h`）

自适应分三块，全部从原固件的 DCMIPP RAW10 → RGB565 预览管线抽出。与原代码差别只有
一点：**DCMIPP 句柄和 pipe 改为入参传入**（原来是文件级静态量），算法/常量完全一致。

### 1) ISP 色调/颜色链（修“发灰发白”）

纯 RAW10→bilinear demosaic→RGB565 没有任何色调/颜色处理，画面发灰。补上最小 ISP 链：
黑电平扣除 → 逐通道白平衡增益（demosaic 前）→ 硬件 gamma。

```c
// 在 preview 管线里、使能 demosaic(RawBayer2RGB) 之后调用一次：
AppCameraISP_ConfigureIspChain(&hdcmipp, DCMIPP_PIPE1);
```

### 2) 自动曝光 AE（1 Hz，走 I2C）

按平均亮度做乘法伺服，增益阶梯 **曝光行 → 模拟增益 → 数字增益**。目标中灰亮度 92
（尺度 0..187 = `2*R5 + G6 + 2*B5`）。内部调用 `AppCameraIMX219_SetExposure`，
必须在允许 I2C 的线程里跑。

### 3) 自动白平衡 AWB（1 Hz，灰世界，无 I2C）

只改 DCMIPP 曝光块的 R/B 增益（Q7，128=x1.0），每次 IIR 走 1/4 步长，带 2% 死区。

### 典型 1 Hz 循环（在 bring-up 线程里）

```c
AppCameraISPStats_t st;
// frame_addr 是最新完成的 RGB565 预览帧；先自行 invalidate D-Cache
AppCameraISP_SampleRgb565((void*)frame_addr, 640, 480, line_pitch, &st);
AppCameraISP_RunAutoExposure(st.luma_avg);
AppCameraISP_RunAutoWhiteBalance(&hdcmipp, DCMIPP_PIPE1, st.r_avg8, st.g_avg8, st.b_avg8);
```

调试量（SWD 可见）：`g_app_camera_ae_exposure_lines/again_code/dgain/update_count`、
`g_app_camera_wb_gain_r_q7/g_q7/b_q7/update_count`。

## 三、硬件依赖（需在工程 `main.h` / CubeMX 中提供）

| 符号 | 本板定义 | 作用 |
|------|----------|------|
| `hi2c4` | I2C4（PE13/PE14） | sensor 与 Tiny1C/BQ27441/EEPROM 共享的控制总线 |
| `CAM_EN_MODULE_Pin/Port` | PG6 | 模组电源使能 |
| `CAM_LED_EN_Pin/Port` | PG4 | 补光灯（上电拉低） |
| `DCMIPP` + `DCMIPP_HandleTypeDef` | DCMIPP PIPE1 | CSI-2 接收 + ISP |

## 四、软件依赖

1. STM32N6 HAL：`HAL_I2C_Mem_*`、`HAL_GPIO_WritePin`、`HAL_Delay`，
   DCMIPP 的 `HAL_DCMIPP_PIPE_SetISPExposureConfig` / `EnableISPExposure` /
   `SetISPBlackLevelCalibrationConfig` / `EnableISPBlackLevelCalibration` /
   `EnableGammaConversion`。
2. 工程现有 `i2c.h`：`app_i2c4_bus_lock_timeout` / `app_i2c4_bus_unlock`。
3. ThreadX（`app_i2c2_bus.c` 用 `tx_mutex_*`）。

> 当前驱动以 20 ms 有限等待访问共享 I2C4。后台 AE 若遇到总线忙应跳过本轮，
> 避免阻塞 Tiny1C 的伪彩、增益和 FFC 控制。

## 五、集成步骤（简要）

1. 把 `Inc/`、`Src/` 加入 include path 与编译列表。
2. 初始化 I2C4、`CAM_EN_MODULE`、`CAM_LED_EN`；I2C4 总线互斥由现有接口按需初始化。
3. `AppCameraIMX219_Init()` → `AppCameraIMX219_SetStream(1)`。
4. 配置 DCMIPP CSI/pipe 为 RAW10 + demosaic（RawBayer2RGB）→ 输出 RGB565，
   然后 `AppCameraISP_ConfigureIspChain(&hdcmipp, pipe)`。
5. 每秒跑一次 `SampleRgb565` + `RunAutoExposure` + `RunAutoWhiteBalance`。

> 注：DCMIPP 的 CSI/VC/pipe/demosaic 基础配置（`HAL_DCMIPP_CSI_*`、`SetISPRawBayer2RGBConfig`
> 等）属于相机管线本体，未包含在本包里；本包只给 sensor 驱动 + 自适应算法。可参考原文件
> `app_camera.c::AppCamera_ConfigureDcmipp`。

## 六、来源

`Program/NECCS_N647/NECCS_N647_App/Appli/Core/{Inc,Src}/`：
- `app_camera_imx219.*`（原样）
- `app_camera_isp.*`（从 `app_camera.c` 抽出的 AE/AWB/ISP 代码，DCMIPP 句柄改为入参）
