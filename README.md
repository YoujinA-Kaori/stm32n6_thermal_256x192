# STM32N6 Tiny1C 256×192 热成像终端

基于 STM32N647、Tiny1C 256×192 热成像模组和 Azure RTOS ThreadX 的嵌入式热像终端工程。

当前稳定主链是：模组原生热像显示、256×192 温度帧处理、128×96 UART 实时温度流、LVGL 触控界面以及 FileX 本地截图/图库。

## 当前功能

- Tiny1C `Image + Temperature` 组合输出
- DCMIPP 接收 `256×384` 逻辑组合帧
- 上半帧 YUV422 原生图像转 RGB565
- 下半帧 packed Y14 解包为 `temp14`，单位为 `kelvin × 16`
- 800×480 LVGL 工业热像仪界面和全屏预览
- 中心温度、高低温点、伪彩、增益和 FFC 控制
- USART1 + DMA，2 Mbps，约 5 fps，128×96 TEMP14 实时流
- FileX + SDMMC2 截图保存和按需图库浏览
- UART 文件模式导出板端截图到 Web 上位机
- BQ27441 电量监测，与 Tiny1C 共用 I2C4 总线互斥

## 分辨率链路

```text
Tiny1C 256×192 Image + 256×192 Temperature
                  │
                  ▼
          DCMIPP 256×384×2 bytes
             ┌────┴────┐
             │         │
      YUV422 image   packed Y14
             │         │
      512×384 RGB565  256×192 temp14
             │         ├── 2×2平均降采样 → UART 128×96
             │         ├── 中心/最高/最低温度
             │         └── 后续AI输入
             └────────────→ LVGL/LCD
```

分辨率公共定义位于 `Appli/Core/Inc/thermal_project_config.h`，Tiny1C BSP 与 UART 流共同使用该配置，避免两处尺寸常量再次漂移。

## AI 当前状态

AI 模型迁移暂未纳入本轮重构。

- 已检入的 CubeAI 生成代码和权重仍对应 `160×120 → 20×15×8` 模型。
- 现有训练集也是 160×120，每帧 38400 字节。
- 固件中的 AI 推理总开关当前为关闭状态，避免把 256×192 缓冲误交给旧模型。
- `thermal_ai/configs/dataset_config.json` 保持与现有旧模型、旧数据集一致。
- `thermal_ai/configs/dataset_config_256x192_target.json` 保存后续原生 256×192 训练目标，但当前不作为默认配置。

恢复 AI 前必须同时完成：采集原生 256×192 数据、重新标注/训练、导出 TFLite、重新生成 CubeAI `thermal.c/.h`、更新 XSPI2 权重以及上板验证。

## 目录说明

| 路径 | 内容 |
|---|---|
| `Appli/Core` | 主程序、ThreadX线程、UART、AI运行时和外设初始化 |
| `STM32CubeIDE/Appli/BSP/Tiny1C` | Tiny1C采集、Y14解包、原生图像显示和控制命令 |
| `STM32CubeIDE/Appli/gui_guider` | LVGL界面、事件和自定义逻辑 |
| `Appli/FileX` | 截图保存、加载、删除和SD目录浏览 |
| `ExtMemLoader/X-CUBE-AI/App` | 当前CubeAI生成代码，暂为160×120版本 |
| `thermal_ai` | PC侧标注、训练、导出和验证工具 |
| `thermal_web` | FastAPI + WebSocket串口上位机 |
| `tools` | TEMP14串口解析和字体生成工具 |

## 构建

推荐在 STM32CubeIDE 中导入根工程 `stm32n6_thermal_256x192`，刷新 `Appli` 子工程后构建 Release。

也可以在已由 CubeIDE 生成 makefile 的环境中执行：

```powershell
cd STM32CubeIDE\Appli\Release
mingw32-make all -j2
```

当前已知不阻塞构建的警告包括：`libirtemp.lib` 的 `wchar_t` ABI 警告、RWX LOAD segment 警告以及部分 debug_info 警告。

## Web 上位机

```powershell
python thermal_web/app.py
```

浏览器访问 `http://127.0.0.1:8000`。同一串口不能同时被其他串口工具占用。

## 硬件接口摘要

- Tiny1C 图像：DCMIPP 8-bit 并行总线
- Tiny1C 控制：I2C4，PE13/PE14
- 串口：USART1，PF12/PF13
- 本地存储：SDMMC2 + FileX
- 显示：LTDC RGB565，LVGL逻辑分辨率800×480
- 触摸：PD14/PD4 软件I2C

## 注意事项

- 不要把 packed Y14 原值直接当作温度；必须先右移两位解包。
- 不要破坏 FileX 的延迟挂载策略，开机阶段主动访问SD可能影响显示启动稳定性。
- Tiny1C VDCMD 互斥和 I2C4 总线互斥都必须保留。
- CubeMX `.ioc` 中的LTDC生成参数与当前板级800×480显示驱动并非完全一致，重新生成代码前应先备份并检查差异。
- `Debug/`、`Release/`、数据集、训练产物和本地采集文件均不纳入Git仓库。
