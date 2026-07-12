# stm32n6_thermal Agent Notes

## 项目定位

这是一个基于 **STM32N647 + Tiny1C(256x192)** 的热成像项目，当前 RTOS 为 **ThreadX**。

当前主目标不是做 MCU 本地伪彩算法，而是：

- LCD 显示 **Tiny1C 模组原生预览图像**
- UART 实时预览默认发送 **128x96 温度流**，由 256x192 temp14 2x2 降采样得到
- 后续 AI / 上位机优先使用 **温度流数据**


## 当前稳定主链

### 模组输出模式

当前默认使用 **Image + Temperature** 组合输出。

- 图像分辨率：`256x192`
- 温度分辨率：`256x192`
- MCU 接收组合帧逻辑高度：`256x384`
- 组合帧布局：
  - 上半帧：`YUV422 / YUYV` 图像
  - 下半帧：`Y14` 温度数据

相关实现：

- `STM32CubeIDE/Appli/BSP/Tiny1C/tiny1c_thermal_app.c`


### LCD 显示链路

当前 LCD 默认显示的是 **模组原生图像**，不是 MCU 用温度流重新做的伪彩。

链路如下：

`Tiny1C -> DCMIPP -> 组合帧拆分 -> YUV422转RGB565 -> 2x显示 -> LCD`

说明：

- 显示分辨率：`512x384`
- LCD 叠加：
  - 中心温度文本
  - 白色十字
- 模组侧伪彩模式可切换，默认值在：
  - `CFG_TINY1C_DEFAULT_PSEUDO_COLOR_MODE`

当前默认值：

- `PSEUDO_COLOR_MODE_5`

注意：

- 这里的“伪彩模式”指的是 **模组原生预览图像的伪彩**
- 不是 MCU 对温度流再做二次着色


### UART 温度流链路

UART 温度流线程仍保留，当前协议：

- 串口：`USART1 + GPDMA1`
- 波特率：`2000000`
- 帧率：`5 fps`
- 数据尺寸：`128x96`
- 包类型：`TEMP14`

说明：

- 上述 `2000000 + 5 fps + 128x96` 是**当前固件默认实时预览温度流配置**。
- 该默认值当前已经和以下路径保持一致：
  - `thermal_web/`
  - `tools/uart_temp14_parser.py`
- `thermal_android_app` 当前**没有**跟随本轮同步，Android 端默认波特率仍是 `921600`；后续如果继续使用手机端，不要默认认为它已自动跟上当前固件默认值。

包头结构定义在：

- `Appli/Core/Src/app_threadx.c`

字段包括：

- `sync_word`
- `packet_type`
- `frame_counter`
- `frame_width`
- `frame_height`
- `payload_bytes`
- `center_temp14`
- `min_temp14`
- `max_temp14`
- `payload`

说明：

- 当前实时预览 `payload` 是 **解包后的 temp14 数据经 2x2 平均降采样后的 128x96 数据**
- `temp14` 语义为：`kelvin * 16`


## 关键数据格式结论

根据工作区文档 `RTOS平台DVP接口软件开发资料 Interface development instructionsV1.0.docx`：

- 每像素 `2 byte`
- **高 14 位有效**
- **小端存储**

因此 MCU 侧不能把收到的 16 位原值直接当温度算，必须先解包。

当前实现已经修正为：

- 在 `tiny1c_thermal_app_unpack_temp14_frame()` 中先做 `Y14` 解包
- 解包后的 `g_tiny1c_temp14_frame[]` 才作为：
  - 中心温度计算输入
  - UART 发送输入
  - 后续 AI / 上位机使用输入

如果再出现“中心温度 900 多度”这类异常，优先检查：

1. 是否又把原始 `Y14 packed word` 直接当 `temp14` 使用
2. 是否改坏了组合帧上下半帧偏移
3. 是否重新引入了未解包的旧路径


## 当前线程职责

### `thermal_thread`

文件：

- `Appli/Core/Src/app_threadx.c`

职责：

- 启动 Tiny1C 应用
- 驱动 `tiny1c_thermal_app_process()`
- 接收 DCMIPP 帧并触发显示处理


### `uart_stream_thread`

文件：

- `Appli/Core/Src/app_threadx.c`

职责：

- 从 `tiny1c_thermal_app_get_temp14_frame()` 取最新温度帧
- 打包 UART 温度流
- 通过 `USART1 DMA` 发出
- 计算中心点温度
- 将中心点补偿结果回写给 LCD 叠加层


## 当前温度补偿状态

### 已接入

`libirtemp.lib` 已接入工程，并通过桥接层解决了 `__hardfp_pow/__hardfp_sqrt` 链接问题。

相关文件：

- `Appli/Core/Inc/libirtemp.h`
- `Appli/Core/Inc/libirtemp_bridge.h`
- `Appli/Core/Src/libirtemp_bridge.c`
- `Appli/Core/Src/libirtemp_hardfp_math.c`
- `STM32CubeIDE/Appli/ThirdParty/libirtemp/`


### 当前实际生效范围

当前 **只对中心温度显示链路做了补偿接入**，还没有把 `libirtemp` 推进到整帧像素级温补。

也就是说：

- LCD 叠加的中心温度：经过补偿
- UART 实时预览 `payload`：当前是从解包后 256x192 原始 `temp14` 做 2x2 平均降采样得到的 128x96 数据
- 整帧显示：当前仍主要依赖模组原生图像链路


## 当前 GUI / LVGL 状态

### GUI 原型

当前已做出一套 `800x480` 横屏工业热像仪 GUI 原型，布局固定为：

- 顶部状态栏
- 左侧设置面板
- 右侧热成像预览区
- 底部快捷栏

主要文件：

- `STM32CubeIDE/Appli/gui_guider/generated/setup_scr_WidgetsDemo.c`
- `STM32CubeIDE/Appli/gui_guider/generated/gui_guider.h`
- `STM32CubeIDE/Appli/gui_guider/generated/events_init.c`


### GUI 与热像链路接入状态

已完成：

- `LVGL` 独立线程运行
- 触摸输入已接入 `LVGL`
- 右侧预览区接 Tiny1C 实时画面
- 伪彩切换已按驱动 `11` 档映射
- 增益切换已绑定
- 自动 `FFC` / 手动 `FFC` 已绑定
- 恢复默认已绑定
- 新增独立 **全屏预览页**
- 全屏页右上角返回按钮文案为 `设置`
- 全屏页保留：
  - 中心十字
  - 中心温度
  - 最高温 / 最低温
  - 右下角伪彩模式名
- 全屏页的热像画面当前不是 `LVGL zoom` 变换，而是：
  - 复用当前 `512x384` 预览源
  - 在 MCU 侧展开成 `512x384 RGB565` 独立缓冲
  - 再交给 LVGL 直接显示
- 最高温 / 最低温白色标记已加黑底圆角小牌子，避免与背景混色

仍属 UI 状态、未深入到底层显示逻辑的项目：

- 镜像
- 翻转
- 中心十字开关
- 中心温度显示开关
- 最高温/最低温标记开关
- 单位切换
- 保存配置持久化


### 当前全屏页实现与性能状态

本轮对话已完成以下实现与调优：

- 全屏页使用单独的 LVGL screen，不再把同一个预览对象在两个 screen 之间来回搬动
- 全屏热像图使用独立 `512x384` 缓冲：
  - `Appli/Core/Src/app_threadx.c`
  - `g_gui_fullscreen_rgb565_frame[]`
- 当前全屏缩放策略是 **整数 2x 复制**，没有引入双线性等插值
- 为降低 GUI 压力，温标叠加刷新已和图像刷新拆开：
  - 热像图像仍按新帧实时刷新
  - 叠加层刷新周期：
    - `CFG_GUI_OVERLAY_UPDATE_PERIOD_MS = 120`
- 为避免在 GUI 线程里同步查模组导致掉帧，最高温 / 最低温坐标查询已改为独立低优先级线程缓存：
  - `extrema_query_thread`
  - `CFG_EXTREMA_QUERY_PERIOD_MS = 200`
  - GUI 线程只读取缓存，不在刷新路径里直接调用 `tiny1c_vdcmd_get_frame_max_min_temp()`

当前结论：

- 全屏模式比普通预览仍更吃资源，这是预期行为
- 当前全屏性能开销主要来自：
  - `512x384` 的整帧 RGB565 展开
  - 全屏页独立图像缓冲占用额外 EXTRAM
- 如果后续仍觉得全屏帧率偏低，优先考虑：
  - 把全屏尺寸从 `512x384` 降到 `480x360`
  - 或直接改为 `512x384` 居中显示
  - 不要优先回到 `LVGL zoom` 方案

### 当前高低温点状态

本轮对话里，高低温标记跟随曾出现“有时固定在同一位置”的观感问题。

已做调整：

- 最高温 / 最低温点的来源不再直接放在 GUI 刷新线程里同步查模组
- 当前由独立线程定期调用：
  - `tiny1c_vdcmd_get_frame_max_min_temp()`
- 结果写入缓存后，再由 GUI 线程读取缓存并更新 marker
- 如果缓存无效，GUI 路径仍会回退到本地扫描 `temp14_frame[]`

注意：

- 由于当前极值缓存线程是 `200 ms` 周期，所以高低温点的响应不会是“每帧级”抖动
- 这属于有意的稳定性/性能折中，不要误判为 bug


### 当前 GUI 启动链稳定性结论

本轮做过以下稳定性修正：

- Tiny1C 启动失败不再直接进 `Error_Handler()`
- GUI 不再死等模块启动
- `LVGL` 首帧会主动触发刷新
- 触摸初始化不再阻塞首帧显示
- 避免在模块启动路径里再次清黑预览界面

涉及文件：

- `Appli/Core/Src/app_threadx.c`
- `Appli/Core/Src/main.c`
- `STM32CubeIDE/Appli/BSP/Tiny1C/tiny1c_thermal_app.c`
- `STM32CubeIDE/Appli/lvgl/porting/lv_port_indev.c`


## 当前中文字体状态

### 问题背景

原先尝试直接使用：

- `lv_font_SourceHanSerifSC_Regular_18`

结论是：

- 大面积挂载该字体时，界面会不稳定，曾出现黑屏
- 小范围挂载时虽然有时可亮屏，但仍不适合作为最终方案


### 当前方案

当前已经生成一个 **超小中文子集字体**，只覆盖本界面实际用到的汉字：

- `STM32CubeIDE/Appli/gui_guider/generated/guider_fonts/lv_font_thermal_cn_18.c`

生成脚本：

- `tools/gen_lvgl_subset_font.py`

当前界面中文字体入口：

- `STM32CubeIDE/Appli/gui_guider/generated/gui_guider.h`
- `STM32CubeIDE/Appli/gui_guider/generated/gui_guider.c`
- `STM32CubeIDE/Appli/gui_guider/generated/setup_scr_WidgetsDemo.c`

字体策略：

- 中文控件用 `lv_font_thermal_cn_18`
- 英文/数字继续走 `lv_font_montserratMedium_19`
- 小字库本身带 `fallback` 到 `Montserrat`


### 最新状态

本轮已确认：

- 字库本身覆盖了 `setup_scr_WidgetsDemo.c` 和 `events_init.c` 中当前实际出现的中文字符
- 还剩少量方块的原因，不是“字库缺字”，而是几处 `lv_label_create()` 直接创建的中文标签没有显式挂 `THERMAL_GUI_CN_FONT`
- 这些位置已经补上

最新补过的关键位置：

- `STM32CubeIDE/Appli/gui_guider/generated/setup_scr_WidgetsDemo.c`

本轮新增确认：

- 因为加入了 `全屏` 按钮，`全` 字曾显示为方块
- 已使用 `tools/gen_lvgl_subset_font.py` 重新生成：
  - `STM32CubeIDE/Appli/gui_guider/generated/guider_fonts/lv_font_thermal_cn_18.c`
- 当前字库已补入 `全`

注意：

- `events_init.c` 里曾经出现过中文乱码字符串，后续如果再改中文文案，务必保证文件编码是 **UTF-8**
- 当前工作区默认 `python` 指向 `D:\STEdgeAI\2.0\Utilities\windows\python.exe`，它不自带 `PIL/pip`
- 如果需要重跑 `tools/gen_lvgl_subset_font.py`，优先复用本机可用的 Python 3.10 + Pillow 环境，不要假设工作区内置 `python` 能直接跑


## 工程文件与构建注意事项

### 当前重要结论

这轮对话里明确确认过：

- **不要把最终方案建立在 `Debug/Release` 生成目录的手改上**
- 最终应该以工程级配置为主，例如：
  - `STM32CubeIDE/Appli/.project`


### 当前已做的工程级变更

已补工程资源登记：

- `STM32CubeIDE/Appli/.project`

其中加入了：

- `gui_guider/generated/guider_fonts/lv_font_thermal_cn_18.c`


### 当前风险

这里要特别注意：

- 我们中途为了快速验证，曾临时改过 `Debug` 下的生成清单
- 这些临时项后来已经按要求撤回
- **撤回后没有再做一次从“纯工程配置”出发的完整 clean build 验证**

因此当前最真实的状态是：

- 代码和字体文件都在
- `.project` 已更新
- 但 **是否仅靠 `.project` + IDE Refresh/Regenerate 就能让 Debug/Release 都重新纳入该字体文件，还需要你在 CubeIDE 里刷新并验证一次**

换句话说：

- 当前“功能逻辑和源文件”是完成的
- 当前“工程收口是否对 Debug/Release 都完全自洽”尚未最终闭环验证


## 当前常用构建命令

在 `STM32CubeIDE/Appli/Debug` 下执行：

- `mingw32-make -f makefile all -j2`

当前已知非阻塞 warning：

- `libirtemp.lib` 的 `wchar_t` ABI warning
- `LOAD segment with RWX permissions`
- 部分 `objdump debug_info` warning

这些 warning 当前不阻塞主要功能。


## 当前明确不再采用的方案

以下方案当前已主动弱化/废弃，不要默认往回加：

- MCU 本地 `Y14 -> 伪彩 -> LCD` 作为主显示链路
- 大量动态范围拉伸 / 本地伪彩 LUT 调优作为主方案
- `SourceHanSerif` 大面积中文字体全局挂载

原因：

- 模组原生图像显示效果更好
- 当前项目更适合“模组原生显示 + MCU 保留温度流”
- 大中文字体在当前板子/工程组合下不稳定


## 关键源码入口

### 主入口与系统初始化

- `Appli/Core/Src/main.c`
- `Appli/Core/Src/app_threadx.c`


### Tiny1C 应用层

- `STM32CubeIDE/Appli/BSP/Tiny1C/tiny1c_thermal_app.c`
- `STM32CubeIDE/Appli/BSP/Tiny1C/tiny1c_thermal_app.h`


### Tiny1C 驱动封装层

- `STM32CubeIDE/Appli/BSP/Tiny1C/tiny1c_vdcmd_app.c`
- `STM32CubeIDE/Appli/BSP/Tiny1C/tiny1c_vdcmd_app.h`
- `STM32CubeIDE/Appli/BSP/Tiny1C/VDCMD/`


### GUI / LVGL

- `STM32CubeIDE/Appli/gui_guider/generated/setup_scr_WidgetsDemo.c`
- `STM32CubeIDE/Appli/gui_guider/generated/events_init.c`
- `STM32CubeIDE/Appli/gui_guider/generated/gui_guider.c`
- `STM32CubeIDE/Appli/gui_guider/generated/gui_guider.h`
- `STM32CubeIDE/Appli/gui_guider/generated/guider_fonts/lv_font_thermal_cn_18.c`
- `tools/gen_lvgl_subset_font.py`


## 对后续 Agent 的建议

1. 先保住“LCD 原生图像显示 + UART 温度流”主链，不要先动显示方向。
2. 如果温度数值异常，优先检查 `Y14` 解包和组合帧拆分，不要先怪伪彩。
3. 如果显示效果问题，优先用模组侧 `pseudo_color_set()` 调整，不要先重做 MCU 本地伪彩。
4. 如果中文又出现方块，先检查：
   - 标签是否显式挂了 `THERMAL_GUI_CN_FONT`
   - `events_init.c` 是否被错误编码污染
   - 新文案是否超出了 `lv_font_thermal_cn_18` 子集
5. 如果需要扩充中文字库，优先继续用 `tools/gen_lvgl_subset_font.py` 增量生成，不要再回到 `SourceHanSerif` 全局挂载方案。
6. 如果全屏页性能不理想，优先怀疑：
   - `512x384` 全屏展开缓冲
   - 全屏页额外图像刷新
   - 不要优先把问题归因到 LVGL 本身
7. 如果最高温 / 最低温点表现异常，优先检查：
   - `extrema_query_thread` 是否正常运行
   - `g_extrema_cache_*` 是否有效更新
   - GUI 是否误走了本地扫描 fallback
8. 高低温点当前是“缓存线程 + GUI 读缓存”架构，不要轻易改回“GUI 线程里同步查模组”
9. 关于构建系统，优先修改工程级配置，不要把最终方案建立在 `Debug/Release` 生成物手改上。
10. 如果要确认 `Release`，最好在 CubeIDE 里对 `Appli` 工程做一次 `Refresh`，然后重新生成并验证对应配置。


## 当前未完成事项

- 还没有把 `libirtemp` 做成整帧像素级补偿
- 还需要继续上板验证环境参数对实际目标的影响
- 还需要继续上板验证：
  - 全屏页长期运行时的实际帧率
  - `extrema_query_thread` 200ms 周期是否是当前最合适折中
  - 全屏 `512x384` 是否需要改成更低分辨率版本
- 还需要最终确认：
  - 仅靠 `.project` 工程级登记后，`Debug/Release` 在 CubeIDE 刷新后是否都会自动纳入 `lv_font_thermal_cn_18.c`
- 如果后续切换分辨率版本，不要复用当前 `256x192` 常量，必须整体复核组合帧尺寸和 UART 协议

## 2026-06-07 BQ27441 Battery Gauge Notes

- 当前工程已接入：
  - `STM32CubeIDE/Appli/BSP/BQ27441/bq27441g1a.c`
  - `STM32CubeIDE/Appli/BSP/BQ27441/bq27441g1a.h`
- `BQ27441` 与 `Tiny1C` 控制通道共用 **硬件 `I2C4`**，不是独立总线。
- 当前采取的稳定化方案不是“GUI 里直接读电池”，而是：
  - `Appli/Core/Src/i2c.c`
  - 新增共享 `I2C4` bus mutex
  - `Tiny1C` VDCMD 路径和 `BQ27441` 轮询路径都走这把总线锁
- 当前 `Tiny1C` 相关互斥关系变成两层：
  - `tiny1c_vdcmd_app.c` 内部的 `VDCMD mutex`
  - `i2c.c` 内部的共享 `I2C4 bus mutex`
- `BQ27441` 当前不是在 GUI 线程里同步读，也不是高频任务：
  - `Appli/Core/Src/app_threadx.c`
  - 新增低优先级 `battery_thread`
  - 默认轮询周期：`1000 ms`
- 当前电池线程只读取最小闭环所需信息：
  - 剩余电量百分比 `SOC`
  - 充电 / 放电 / 空闲状态
- 当前 GUI 集成方式：
  - 右上角 `status_power` badge
  - GUI 只读缓存，不直接访问 `BQ27441`
  - 文案当前为 ASCII：
    - `CHG xx%`
    - `DSG xx%`
    - `BAT xx%`
    - 无效时 `BAT --%`
- 如果后续这块出问题，优先检查：
  1. `BQ27441` 是否真的仍挂在 `I2C4`
  2. `app_i2c4_bus_lock()` / `app_i2c4_bus_unlock()` 是否被破坏
  3. 是否有人又在 GUI 或别的高优先级路径里直接访问了 `BQ27441`
  4. `tiny1c_vdcmd_app.c` 是否还保留了共享总线锁接入

## 2026-05-16 Hardware Expansion Notes

Conversation summary for the next session:

- Software validation has been completed on the official STM32N6 development board.
- The next phase is custom hardware design: a core board plus a dedicated expansion board.
- The user's core board is assumed to already integrate the display interface, touch interface, XSPI, and EEPROM.

Confirmed hardware conclusions from this conversation:

- Touch control is not on the same bus as Tiny1-C control.
- Touch uses a software I2C on `PD14` and `PD4`.
- Tiny1-C control uses hardware `I2C4` on `PE13` and `PE14`.
- `I2C4` is shared in the current project by Tiny1-C control and onboard EEPROM.
- The UART used by the project is `USART1` on `PF12` and `PF13`.

Tiny1-C video capture path confirmed in the project:

- `PD7` -> `DCMIPP_D0`
- `PE6` -> `DCMIPP_D1`
- `PE0` -> `DCMIPP_D2`
- `PB9` -> `DCMIPP_D3`
- `PE8` -> `DCMIPP_D4`
- `PE5` -> `DCMIPP_D5`
- `PH9` -> `DCMIPP_D6`
- `PB7` -> `DCMIPP_D7`
- `PD0` -> `DCMIPP_HSYNC`
- `PB8` -> `DCMIPP_VSYNC`
- `PD5` -> `DCMIPP_PIXCLK`

Expansion-board implication:

- If LCD, touch, XSPI, and EEPROM are already on the core board, then the expansion board mainly needs Tiny1-C related signals.
- The must-consider signal groups are:
  - DCMIPP parallel capture bus
  - hardware `I2C4` control bus
  - `USART1` if serial logging/debug is still needed externally
  - power and ground

Important clarification:

- `DCMIPP` is a bus, not a single pin.
- `I2C4` and `USART1` are not substitutes for `DCMIPP`; each serves a different purpose.
- For PC-side serial logging/debug only, the minimum external header is usually `USART1 + GND`, not the full Tiny1-C bus.

Likely next task in the next conversation:

- Produce a final "must-route" vs "optional-route" connector checklist for the custom expansion board.

## 2026-05-19 Web Viewer Notes

- 新增 `thermal_web/` 作为独立 Web 上位机目录，基于 `FastAPI + WebSocket + Canvas`。
- 入口：`python thermal_web/app.py`，浏览器打开 `http://127.0.0.1:8000`。
- 默认自动选择第一个可用串口，也可以在网页里 `Refresh / Connect` 切换。
- 当前 Web 端默认串口波特率已同步为：`2000000`
- 当前 Web 端实时热像数据刷新率，受固件串口温度流节拍限制，默认约为：`5 fps`；实时流尺寸为 `128x96`。
- 网页当前功能只保留和演示相关的最小闭环：
  - 热图实时显示
  - `Center / Max / Min`
  - 伪彩模式切换
  - 温标范围滑条
  - 截图
- Web 端图库相关能力当前仍保留：
  - `同步图库`
  - `下载最新`
  - `下载选中`
- Web 端直接复用 `tools/uart_temp14_parser.py` 的串口帧格式，解析的是原始 `temp14` payload。
- 图片横幅已移动到 `thermal_web/static/assets/thermal_ai_banner.png`，并放在左侧栏顶部作为品牌图。
- 同一个串口不能被别的程序占用；网页会自己打开/关闭串口，不需要再单独开串口监视器。

## 2026-05-27 本地截图 / FileX 记录

- 本轮已把本地截图存储功能正式接入固件，并打通了可实际使用的端到端闭环。
- 本轮最终使用的存储链路为：
  - `ThreadX`
  - `FileX`
  - `Appli/FileX/Target/fx_stm32_sd_driver_glue.c`
  - `STM32CubeIDE/Appli/BSP/SD_NAND/sd_nand.c`
  - `HAL_SD`
  - `SDMMC2`
- 本轮最终的重要决策：
  - 开机阶段不要主动挂载或扫描 SD 介质。
  - `FileX` 在启动时只做轻量初始化，真正的 SD 挂载延后到用户第一次保存截图或打开图库时再执行。
  - 这个“懒加载挂载”策略修复了此前“过早触发 SD/FileX 访问导致显示启动不稳定，甚至出现黑屏或花屏”的问题。

### 当前本地存储行为

- 截图保存入口：
  - 主界面底部按钮可将当前热像预览保存为 BMP。
  - 全屏页也新增了独立的拍照按钮。
- 图库入口：
  - 原先的 `device info` 卡片不再承担文件系统入口功能。
  - 当前在 `系统` 页签中新增独立 `图库` 按钮，点击后进入图库页。
- 文件位置与命名：
  - 目录：`/THERMAL`
  - 文件名：`THM00001.BMP`、`THM00002.BMP` ...
- 图库当前支持：
  - 打开最新截图
  - 上一张 / 下一张
  - 删除当前图片

### 截图渲染说明

- 保存出来的图片是 BMP RGB565 文件，底图来自当前 LVGL 实时热像预览，再叠加重新绘制的温标信息。
- 当前叠加内容包括：
  - 中心十字
  - 中心温度
  - 最高温
  - 最低温
  - 高低温标记
- 保存图中的最高温 / 最低温温度牌，已经从“跟随对象相对位置”改成“固定安全区域布局”，避免被裁剪。
- 保存图中的高低温 marker 文案当前已强制固定为：
  - 最高温 marker：`高`
  - 最低温 marker：`低`
  这样可以避免依赖运行时界面对象上的文本内容。
- 图库里的预览图当前已经支持缩放自适应，因此全屏截图在图库面板中查看时不会再被裁切。

### 本轮中文字库说明

- 小字库 `lv_font_thermal_cn_18.c` 本轮已重新生成。
- 本轮新增确认必须覆盖的汉字包括：
  - `图`
  - `库`
  - `拍`
  - `照`
  - `高`
  - `低`
- 所有直接显示中文的新按钮，必须显式挂载 `lv_font_thermal_cn_18`。
- 如果后续新增中文再次出现方块，优先检查：
  1. 该控件是否显式使用了 `lv_font_thermal_cn_18`
  2. 是否已用 `tools/gen_lvgl_subset_font.py` 重新生成子集字体
  3. 不要回退到全局挂载 `SourceHanSerif` 的旧方案
- 本轮重新生成字体时，实际使用的是：
  - `C:\Users\26218\AppData\Local\Programs\Python\Python310\python.exe`
  - 且该环境已安装 Pillow
  原因是默认 `D:\STEdgeAI\2.0\Utilities\windows\python.exe` 仍然缺少 `PIL/pip`

### 本轮主要改动文件

- `Appli/FileX/App/app_filex.c`
- `Appli/FileX/App/app_filex.h`
- `Appli/FileX/Target/fx_stm32_sd_driver_glue.c`
- `Appli/FileX/Target/fx_stm32_sd_driver.h`
- `Appli/AZURE_RTOS/App/app_azure_rtos.c`
- `Appli/AZURE_RTOS/App/app_azure_rtos_config.h`
- `Appli/Core/Src/app_threadx.c`
- `STM32CubeIDE/Appli/gui_guider/custom/custom.c`
- `STM32CubeIDE/Appli/gui_guider/generated/guider_fonts/lv_font_thermal_cn_18.c`
- `STM32CubeIDE/Appli/BSP/SD_NAND/`

### 本轮之后仍需继续关注

- 继续做焊接式 2GB SD NAND 上的长时间保存 / 打开 / 删除稳定性验证。
- 如果后续截图可靠性再次波动，优先怀疑：
  - SD 挂载时机是否又提前了
  - `fx_stm32_sd_driver_glue.c` 的 glue 层行为是否被改坏
  - BSP `sd_nand` 路径是否仍能稳定返回成功
- 后续如果再修改文件系统，除非有非常强的理由，否则不要破坏当前“懒加载挂载”策略。

## 2026-05-31 Web 串口图库导出记录

### 本轮目标

- 在不增加额外硬件接口的前提下，复用当前 `USART1`，让 PC/Web 端能够把焊死 SD NAND 里的截图文件导出到电脑。
- 当前最终方向不是“实时流和文件流并行复用同一时刻的串口”，而是：
  - `STREAM_MODE`：继续发送实时 `TEMP14`
  - `FILE_MODE`：暂停实时流，只处理文件命令

### 当前固件侧结论

- 已在 `Appli/Core/Src/app_threadx.c` 中加入 UART 文件模式控制：
  - `APP_UART_FILE_HOLD_GUI`
  - `APP_UART_FILE_HOLD_WEB`
- 当前文件模式采用“持有位”而不是简单开关：
  - GUI 进入图库时可持有
  - Web 发起文件事务时也可持有
  - 只有所有持有位都释放后，实时流才恢复
- `uart_stream_thread` 当前会在文件模式激活时暂停 `TEMP14` 发送。
- 已新增 `uart_command_thread`，负责处理最小文件协议命令。

### 当前串口文件协议

- 当前已实现命令：
  - `FILE_ENTER`
  - `FILE_EXIT`
  - `FILE_LIST`
  - `FILE_GET_LATEST`
  - `FILE_GET <filename>`
- 当前返回格式：
  - `OK ...`
  - `ERR ...`
  - `LIST <count>`
  - `NAME <filename>`
  - `LIST_END`
  - `FILE_BEGIN <filename> <width> <height> <payload_bytes>`
  - `DATA <seq> <base64>`
  - `FILE_END`
- 当前文件传输不是直接回传原始 BMP 文件字节流，而是：
  - MCU 先从 SD 中加载 BMP
  - 取出 RGB565 像素数据
  - 分块 Base64 发送
  - PC/Web 端再重建 BMP 文件

### 当前 Web 端实现状态

- `thermal_web/` 已补充图库导出能力。
- 当前 Web 端新增接口：
  - `POST /api/gallery/list`
  - `POST /api/gallery/download-latest`
  - `POST /api/gallery/download`
- 当前前端页面已支持：
  - `同步图库`
  - `下载选中`
  - 列出 SD 中的 `THMxxxxx.BMP`
  - 点击文件名选中后下载
- 下载结果保存在：
  - `thermal_web/downloads/`

### 当前时序与稳定性结论

- 本轮已经确认：
  - **直接在 Web 端点“同步图库/下载”并不是 100% 稳定**
  - **先在板子本地进入一次图库，再回到 Web 端操作，成功率明显更高**
- 这说明当前最真实的问题不是协议方向错误，而是：
  - 第一次进入文件模式时
  - 第一次触发 FileX/SD 准备时
  - 时序仍然偏紧
- 已做过的缓解包括：
  - `FILE_ENTER` 中先调用 `app_filex_prepare()`
  - `app_filex_prepare()` 当前带 3 次重试
  - Web 端图库同步/下载也带 3 次重试
  - Web 端串口事务开始前会清空输入缓冲
  - Web 端协议行读取已从 `readline()` 改为逐字节等到完整换行，减少半行 Base64 导致的 padding 错误
  - Web 端同步/下载逻辑已放入线程池，避免 FastAPI 主循环被阻塞
  - Web 端前端请求带超时，避免页面一直卡在“正在同步”
  - 固件侧增加了 Web 文件模式超时自动恢复，避免异常后长期停流

### 当前已确认的可用操作规程

- 当前最稳的使用方式是：
  1. 板子开机
  2. 先进入一次本地 `图库`
  3. 再退出回主界面
  4. 打开 Web 页面并连接串口
  5. 点 `同步图库`
  6. 在列表中点选目标文件
  7. 点 `下载选中`
- 结论：
  - 这套流程当前已经可实际使用
  - 不必继续在本轮追求“第一次直接点 Web 也 100% 成功”
  - 后续可把“先进本地图库预热”作为当前阶段的稳定操作规程

### 对后续 Agent 的建议

- 如果后续继续优化这条链，优先把目标定为：
  - 不先进本地图库也能稳定 `FILE_ENTER`
  - 而不是重新推翻当前协议
- 如果再次出现以下错误，优先怀疑仍是“第一次文件模式准备时序”问题：
  - `串口命令超时`
  - `串口协议重同步超时`
  - `FILE_ENTER 失败: ERR Storage Err`
- 如果再次出现 Base64 错误，如：
  - `Incorrect padding`
  - `Invalid base64-encoded string`
  优先检查 Web 端协议读取是否又退回到了非整行读取。
- 当前已经确认固件支持按文件名下载：
  - `FILE_GET <filename>`
  后续如需扩展“删除选中”“下载上一张/下一张”，优先在现有协议上加，不要重做整体框架。

## 2026-05-31 Tiny1C 控制互斥与极值刷新记录

### 本轮问题背景

- 本轮出现了一个新现象：
  - 伪彩切换
  - 增益切换
  - 手动 FFC
  有时需要点两三下才真正生效
- 该问题在接入文件系统前用户体感上没有出现。
- 用户进一步确认：
  - 按钮本身有按下反馈
  - `CLICKED` 逻辑大概率已经触发
  - 即使完全不开 Web、也不进入本地图库，这个现象仍然存在
- 同时用户确认：
  - 只有“发 Tiny1C 控制命令”的这些动作受影响
  - 纯本地 UI 项没有类似问题

### 本轮最终判断

- 当前最合理的根因判断是：
  - **Tiny1C 的 VDCMD/I2C 控制通道存在多线程并发访问**
  - 文件系统接入后新增线程与整体调度相位变化，把这个原本未显性的边界问题放大了
- 当前最关键的并发访问源包括：
  - GUI 回调中的控制命令：
    - `tiny1c_vdcmd_set_pseudo_color(...)`
    - `tiny1c_vdcmd_set_gain_mode(...)`
    - `tiny1c_vdcmd_trigger_ffc()` / `tiny1c_thermal_app_force_ffc()`
  - 后台查询线程中的读取命令：
    - `tiny1c_vdcmd_get_frame_max_min_temp(...)`
    - `tiny1c_vdcmd_get_point_temp(...)`
  - 以及启动阶段和其他链路里的 VDCMD 包装函数
- 结论：
  - 该问题不是简单的 LVGL 触摸命中问题
  - 而更像“控制命令偶发第一次没有成功发到模组”

### 本轮已验证有效的修复

- 当前已在：
  - `STM32CubeIDE/Appli/BSP/Tiny1C/tiny1c_vdcmd_app.c`
  加入统一的 `ThreadX TX_MUTEX`
- 具体做法：
  - 在 `tiny1c_vdcmd_app.c` 内部创建全局 VDCMD mutex
  - 所有 `tiny1c_vdcmd_*` 包装函数进入底层命令前统一：
    - `tx_mutex_get(...)`
    - 退出后 `tx_mutex_put(...)`
- 这样做后，用户反馈：
  - 伪彩切换恢复正常
  - 增益切换恢复正常
  - 手动 FFC 恢复正常
- 因此：
  - **当前应保留这把 VDCMD mutex**
  - 不建议再回退掉这部分修复

### 本轮出现的副作用

- 加互斥后，最高温 / 最低温标记刷新体感变慢
- 当前原因判断：
  - 不是 GUI 帧率下降
  - 而是 `extrema_query_thread` 每轮都要走多次模组控制读取
  - 这些读取现在经过 mutex 串行化后，真实查询耗时更明显

### 关于极值刷新周期的实际测试结论

- 本轮先后测试了：
  - `200ms`
  - `120ms`
  - `80ms`
  - `50ms`
- 用户反馈：
  - 把 `CFG_EXTREMA_QUERY_PERIOD_MS` 一路压低后
  - **最高温 / 最低温标记刷新体感几乎没有明显提升**
- 这说明：
  - 当前瓶颈不在“线程睡眠周期”
  - 而在“每轮模组查询本身的耗时”
- 结论：
  - **继续一味压低查询周期意义不大**
  - 只会更频繁占用 VDCMD mutex
  - 有可能再次影响伪彩/增益/手动 FFC 的交互稳定性

### 本轮放弃的思路

- 曾尝试过把极值线程里的部分模组查询替换成本地 `temp14_frame[]` 推导，以减少 VDCMD 调用次数
- 用户明确反馈：
  - 模组函数给出的结果更准
  - 本地替代方案不符合预期
- 因此本轮已撤回该尝试
- 后续默认策略：
  - 高低温相关结果优先保留模组原始函数路径
  - 不要为了刷新速度轻易改成 MCU 本地近似值

### 当前建议给后续 Agent

- 保留：
  - `tiny1c_vdcmd_app.c` 中的统一 VDCMD mutex
- 不建议默认继续：
  - 单纯继续把 `CFG_EXTREMA_QUERY_PERIOD_MS` 往更小压
- 如果后续还要优化高低温标记刷新，优先考虑：
  - 优化“每轮模组查询组织方式”
  - 而不是单纯调小周期
- 但若用户明确强调“模组函数最准”，则不要默认改成：
  - 本地 `temp14` 扫描
  - 本地极值替代
- 当前这轮结论可以概括为：
  - **控制命令稳定性优先通过 VDCMD mutex 解决**
  - **高低温标记刷新慢属于模组查询耗时带来的自然代价，不能简单靠缩短线程周期解决**

## 2026-06-04 Thermal AI Detection Notes

- `thermal_ai/` has now been upgraded from `classification + heatmap` into a **lightweight object-detection workflow**.
- Current retained top-level classes are:
  - `empty`
  - `person`
  - `hand`
  - `hot_object`
  - `circuit_board_normal`
  - `circuit_board_abnormal_hotspot`
- Detector-active classes are all non-empty classes above.
- The current AI route is:
  - raw `temp14 .bin`
  - bbox sidecar `.json`
  - lightweight anchor-free grid detector
  - `Keras -> TFLite -> CubeAI`

### Detection Training / Annotation

- The annotation tool file is still named:
  - `thermal_ai/scripts/annotate_centerpoints.py`
- But it is now a **bounding-box annotation tool**, not a center-point tool.
- Sidecar format is now `bbox xyxy`, with:
  - `x_min`
  - `y_min`
  - `x_max`
  - `y_max`
- Dataset / detector config:
  - `thermal_ai/configs/dataset_config.json`
- The detector is a lightweight `15x20` grid head, not YOLO.

### Coordinate Alignment Conclusion

- A key fix in this round is that **UART temp14 stream orientation is now aligned with the current preview orientation**.
- LCD default preview still uses:
  - mirror enabled
  - flip enabled
- UART packet payload generation in `Appli/Core/Src/app_threadx.c` now applies the same orientation before sending the frame.
- Therefore the following are intended to share the same coordinate convention:
  - LCD default preview
  - new UART-exported temp14 stream
  - thermal AI annotation preview
  - thermal AI detection output coordinates

### Board-Side Coordinate Transform Helper

- New helper API added:
  - `tiny1c_thermal_app_transform_frame_point()`
- Files:
  - `STM32CubeIDE/Appli/BSP/Tiny1C/tiny1c_thermal_app.h`
  - `STM32CubeIDE/Appli/BSP/Tiny1C/tiny1c_thermal_app.c`
- Purpose:
  - convert one raw temp14 frame coordinate into the current preview-oriented coordinate
  - keep overlays and future AI boxes synchronized with `mirror/flip`

### Thermal AI Runtime Module

- A new standalone runtime module has been added:
  - `Appli/Core/Inc/thermal_ai_runtime.h`
  - `Appli/Core/Src/thermal_ai_runtime.c`
- This module is intended to avoid further bloating `app_threadx.c`.
- It currently provides:
  - AI result structures
  - detection / bbox structures
  - application mode enum
  - alarm state enum
  - consecutive-frame abnormal confirmation
  - capture latch for evidence snapshot
  - frame-to-preview point scaling helper
  - frame-to-preview bbox scaling helper

### Runtime Integration Status

- `thermal_ai_runtime.c` has been added to project metadata in:
  - `STM32CubeIDE/Appli/.project`
- The source itself has been compiled successfully with `arm-none-eabi-gcc`.
- A managed build from `STM32CubeIDE/Appli/Debug` also completes successfully.
- If CubeIDE does not immediately show the new file in the project tree, do:
  - `Refresh`
  - then re-check managed project resource sync

### Recommended Next Step

- Continue using `thermal_ai_runtime.*` as the single home for:
  - alarm policy
  - application mode policy
  - AI result cache
  - bbox scaling helpers
- Keep `app_threadx.c` focused on:
  - thread scheduling
  - data acquisition
  - calling runtime helpers
- Keep GUI-facing rendering logic mainly in:
  - `STM32CubeIDE/Appli/gui_guider/custom/custom.c`

## 2026-06-05 Thermal AI Detection Scope Update

- The `hand` class has now been removed from the active thermal detector workflow.
- Current active detector classes are:
  - `person`
  - `hot_object`
  - `circuit_board_normal`
  - `circuit_board_abnormal_hotspot`
- `empty` is still retained as a non-box background / no-target class.

### Dataset Directory State

- `thermal_ai_dataset/raw/` has been normalized to keep only:
  - `empty`
  - `person`
  - `hot_object`
  - `circuit_board_normal`
  - `circuit_board_abnormal_hotspot`
- Legacy raw directories such as:
  - `hand`
  - `circuit_board_hotspot`
  should no longer be used for new data collection.

### Toolchain State

- The bbox annotation tool now effectively uses 4 detection classes.
- Numeric class mapping is now:
  1. `person`
  2. `hot_object`
  3. `circuit_board_normal`
  4. `circuit_board_abnormal_hotspot`
- `thermal_ai/configs/dataset_config.json` is the current source of truth for class order.

### Productization Direction

- The main recommended product scenario is now:
  - abnormal board hotspot detection
  - temperature-delta threshold linkage
  - consecutive-frame confirmation
  - screenshot evidence capture
- Hand detection is no longer part of the recommended default product path.

## 2026-06-21 NPU / CubeAI Bring-Up Notes

- 本轮已确认：
  - **当前板端 NPU 推理主链已经跑通**
  - 能看到跟随实时场景变化的框时，说明不是“还没开始推理”，而是 `LL_ATON_RT_Main(&NN_Instance_thermal)` 已经真正执行并产生了有效输出

### 当前最重要的硬件/底层注意事项

- 对 STM32N6 上这条 CubeAI/NPU 路径，**只打开 AXISRAM 时钟不够**。
- 除了：
  - `__HAL_RCC_RAMCFG_CLK_ENABLE()`
  - `__HAL_RCC_AXISRAM3/4/5/6_MEM_CLK_ENABLE()`
  还必须确保：
  - `SRAM3/4/5/6` 没有停留在 `shutdown`
  - 需要执行与 `HAL_RAMCFG_EnableAXISRAM()` 等价的 **power-on** 动作
- 本项目本轮最关键的收口点就是这里：
  - 如果 AXISRAM5 输入区/输出区表面地址正常，但 NPU 输出始终像没写回，很可能不是权重没烧，而是 `AXISRAM` 实际没真正 power on
- 一个强信号是：
  - 右上角 AI 状态长期停在类似 `AIWI 43 00`
  - 其中：
    - `W` 说明 CPU 侧读 `0x71000000` 权重签名是对的
    - `43` 说明 objectness 接近“全 0 输出”时的固定值
    - `00` 说明输出首字节就是 `0x00`
  - 这种情况下，优先怀疑：
    - `AXISRAM power-on / RAMCFG`
    - NPU 输出回写链路
  - 不要先回头怀疑 GUI

### 当前权重烧录注意事项

- **应用固件 hex 不等于 AI 权重也被烧进去**
- 当前 AI 权重文件需要单独烧录：
  - `thermal_atonbuf.xSPI2.raw`
  - `thermal_atonbuf.xSPI2.bin`
  - `thermal_atonbuf.xSPI2.hex`
- 当前这版 `thermal` 模型确认使用的外部权重地址窗口是：
  - `0x71000000 ~ 0x7100F130`
- 所以以后如果再次出现：
  - 程序能跑
  - GUI 正常
  - 但 AI 永远没有有效输出
  不要默认认为“权重一定已经在位”，应单独核对：
  - 当前模型生成报告
  - 当前烧录的 `atonbuf` 文件
  - 当前外部 Flash 映射地址

### 当前 AI 调试注意事项

- 本轮为定位问题，曾加入 **reference input** 调试模式：
  - `CFG_THERMAL_AI_USE_REFERENCE_INPUT`
- 该模式打开时，板端不会吃实时温度流，而是反复喂固定参考样本。
- 典型现象是：
  - 总能稳定出框
  - 框位置基本不变
  - 看起来“AI 成功了”，但不跟现场目标移动
- 因此：
  - 只要是准备给用户实际使用/验证实时效果，必须确认：
    - `CFG_THERMAL_AI_USE_REFERENCE_INPUT == 0U`

### 当前板端后处理接口假设

- 这版板端 `app_threadx.c` 不是“任意模型都能直接替换”，当前写死了以下接口假设：
  - 输入：
    - `256 x 192 x 1`
  - 检测头网格：
    - `20 x 15`
  - 每格输出通道：
    - `8`
  - 当前类别顺序：
    - `person`
    - `circuit_board_normal`
    - `circuit_board_abnormal_hotspot`
  - 网络实例名：
    - `thermal`
- 因此以后如果重新训练新模型：
  - **只有在输入尺寸、输出张量形状、类别顺序、网络名都兼容时，才适合直接替换 `thermal.c/.h + atonbuf`**
  - 如果改成：
    - 仅 `person` 单类
    - 不同 grid
    - 不同输出通道数
    - YOLO/分类等别的头
    - 网络名不叫 `thermal`
    就不能只替换生成文件，必须同步修改：
    - `Appli/Core/Src/app_threadx.c`
    - 输入预处理
    - 输出 decode
    - 类别映射
    - 阈值/NMS

### 当前模型误检现象的判断边界

- 当出现以下情况时：
  - NPU 已稳定运行
  - 实时场景里人能大体识别出来
  - 但空背景也有两个框
  - 且框分数总在较高区间
  当前更应判断为：
  - **模型/训练/后处理阈值问题**
  - 不再是“NPU 没跑起来”
- 也就是说：
  - “能实时跟人动” 与 “误检仍然很多” 可以同时成立
  - 前者说明板端通路通了
  - 后者说明模型还需要继续优化

### 对后续 Agent 的强提醒

1. 如果 AI 再次完全无输出，先看：
   - `AXISRAM power-on`
   - 权重是否单独烧录
   - `reference input` 是否误开
   - 不要先怪 GUI
2. 不要把“能编过”当成“能推理”。
   - 这条链路里，编译成功、程序运行、NPU 实际出有效输出，是三件不同的事
3. 不要默认新模型都能直接替换。
   - 先核对输入/输出/类别接口是否与 `app_threadx.c` 当前假设一致
4. 不要把最终方案建立在 `Debug/Release` 生成物手改上。
   - 仍然优先修改 `.project` / `.cproject` / 工程源文件

## 2026-06-21 Future Model Replacement Workflow

- 当前项目已经明确进入一个新的维护阶段：
  - **后续如无必要，不要为了迭代模型再次依赖 CubeMX 整体重新生成工程代码**
- 原因：
  - 本项目 `CubeMX/CubeIDE` 重新生成后，往往会冲掉或扰动已经人工收口好的：
    - NPU / IAC / XSPI2 / AXISRAM 相关初始化
    - ThreadX / AI 接入代码
    - GUI / FileX / Tiny1C 已验证稳定的工程级改动
  - 重新收口一次的成本明显高于“直接替换兼容模型”

### 推荐的后续默认策略

- 以后如果只是继续训练更好的 **兼容模型**，默认优先走：
  - **直接替换旧模型生成物**
  - **不要重新跑 CubeMX 工程生成**
- 这里的“兼容模型”指的是以下条件同时满足：
  - 网络名仍叫：
    - `thermal`
  - 输入仍是：
    - `256 x 192 x 1`
  - 板端预处理语义仍然一致：
    - `temp14 -> 背景相对归一化 -> int8`
  - 检测头仍然兼容当前板端 decode：
    - `20 x 15` grid
    - `8` output channels
  - 类别顺序仍然兼容当前板端映射：
    - `person`
    - `circuit_board_normal`
    - `circuit_board_abnormal_hotspot`

### 兼容模型的最小替换范围

- 若新模型满足上述兼容条件，后续默认只替换这些文件：
  - `ExtMemLoader/X-CUBE-AI/App/thermal.c`
  - `ExtMemLoader/X-CUBE-AI/App/thermal.h`
  - `ExtMemLoader/X-CUBE-AI/App/thermal_generate_report.txt`
  - `thermal_atonbuf.xSPI2.raw`
  - `thermal_atonbuf.xSPI2.bin`
  - `thermal_atonbuf.xSPI2.hex`
- 然后：
  - 重新编译 `Appli`
  - 重新烧录应用固件
  - 重新烧录新的 `atonbuf` 权重文件

### 默认不要随新模型一起重生成/重覆盖的部分

- 如果只是兼容模型替换，默认**不要**顺手重做或重覆盖这些内容：
  - `stm32n6_thermal.ioc`
  - `CubeMX` 重新生成的外设初始化代码
  - 已人工修正过的 `main.c`
  - 已人工修正过的 `app_threadx.c`
  - 已人工修正过的 `stm32n6xx_it.c`
  - 已人工修正过的 `.project / .cproject`
  - 已稳定接入的 `Middlewares/ST/AI` 与 `ll_aton` 运行时
- 换句话说：
  - **模型迭代默认是“替换模型资产”问题**
  - 不要把它重新升级成“整工程重生成”问题

### 只有在这些情况出现时，才允许超出“直替换”范围

1. 新模型输入尺寸变了
2. 新模型输出张量形状变了
3. 新模型类别数量或顺序变了
4. 新模型不再使用当前这套轻量 grid detector 头
5. 新模型网络名不再是 `thermal`
6. 新模型依赖了不同版本的 `X-CUBE-AI / ll_aton / runtime`

- 只有在这些情况下，才应进入：
  - 修改 `app_threadx.c`
  - 修改 decode / 映射 / 阈值
  - 必要时调整工程配置
- 即使如此，也仍然应该：
  - 先做最小人工改动
  - **不要优先回到 CubeMX 全量重生成**

### 后续替换模型时的快速检查清单

1. 核对 `thermal_generate_report.txt`
   - 输入尺寸
   - 输出尺寸
   - 权重地址
2. 核对 `app_threadx.c`
   - `CFG_THERMAL_AI_INPUT_*`
   - `CFG_THERMAL_AI_GRID_*`
   - `CFG_THERMAL_AI_OUTPUT_*`
   - 类别映射
3. 确认 `CFG_THERMAL_AI_USE_REFERENCE_INPUT == 0U`
4. 重新烧录新的 `atonbuf`
5. 上板先看右上角 AI 状态
   - 如果重新回到类似 `AIWI 43 00`
   - 优先查权重 / AXISRAM / 输出是否全零
   - 不要先怀疑“新模型一定训练坏了”

### 对后续 Agent 的强提醒

- 用户已经明确表达：
  - **不希望以后为了换模型再次依赖 CubeMX 生成代码**
- 因此后续只要用户说“换一个新模型试试”，默认理解应是：
  - 先评估是否属于“兼容模型直替换”
  - 优先给出“最小替换文件清单”
  - 不要默认建议“重新用 CubeMX 生成一遍工程”

## 2026-07-12 温度流极值刷新改造

### 当前最终实现

- 最高温、最低温、中心温度及高低温坐标全部来自已经校正的 `256x192 temp14` 温度帧。
- 不再周期调用：
  - `tiny1c_vdcmd_get_frame_max_min_temp()`
  - `tiny1c_vdcmd_get_point_temp()`
- VDCMD/I2C 控制通道只保留伪彩、增益、FFC 等用户控制命令；不要恢复后台持续查询。
- 极值计算已经合并进 `tiny1c_thermal_app_unpack_temp14_frame()`：
  - 像素完成 Y14 解包和温度校正后，顺便累计中心温度、最高温、最低温及零基坐标。
  - 不再对整帧做第二次 `49152` 像素扫描。
- BSP 使用序列号保护的原子快照发布结果：
  - 类型：`tiny1c_thermal_extrema_t`
  - 接口：`tiny1c_thermal_app_get_frame_extrema()`
  - 读取方不会拿到跨帧混合的温度和坐标。
- `app_threadx.c` 中的 `extrema_update_thread` 每 `125 ms` 读取一次快照，约 `8 Hz`。
- GUI 叠加层刷新周期也为 `125 ms`，高低温标记和文本保持同一节拍。

### 已确认的失败方案

- 不要重新采用“独立线程扫描共享 `g_tiny1c_temp14_frame[]`，再比较扫描前后帧号”的方案。
- Tiny1C 当前预览约 `25 fps`，共享缓冲会持续被下一帧重写；完整扫描期间帧号经常变化，结果会被反复丢弃。
- 该失败方案的典型现象是：
  - 大部分时间高低温位置和温度卡住
  - 偶尔线程相位错开后连续刷新一小段时间
- 正确方向是继续保留“解包同循环计算 + 原子快照 + 8 Hz GUI读取”。

### 构建验证

- `STM32CubeIDE/Appli/Release` 下执行 `mingw32-make all -j2` 已通过。
- 原有 `_close/_read/_write` 等 nosys 警告和 `libirtemp wchar_t` 警告仍存在，但不阻塞链接和产物生成。
