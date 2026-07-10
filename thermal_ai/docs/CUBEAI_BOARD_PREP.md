# CubeAI 板端接入说明

## 当前模型边界

当前检入的CubeAI网络仍是160×120版本：

- 输入：`1×120×160×1`，int8
- 输出：`1×15×20×8`，int8
- 输入长度：19200字节
- 输出长度：2400字节

Tiny1C固件主链已经是256×192，因此本轮暂时关闭板端AI推理。不要通过删除长度检查或强制修改 `thermal.h` 宏来启用旧模型。

## 板端输入原则

模型输入只能来自解包后的原始 `temp14`：

- 不使用LCD伪彩图
- 不使用RGB565显示缓冲
- 不使用YUV原生图像
- 训练端与板端必须使用相同的镜像、翻转和归一化约定

## 当前保护机制

`app_threadx.c` 在取得CubeAI输入输出描述后，会检查生成网络缓冲区长度。长度与应用配置不一致时直接停止推理，防止缓冲区越界。

当前还通过 `CFG_THERMAL_AI_ENABLE = 0` 关闭推理入口。恢复该开关前应确认：

- `LL_ATON_THERMAL_IN_1_SIZE_BYTES == 49152`
- `LL_ATON_THERMAL_OUT_1_SIZE_BYTES == 6144`
- 生成网络输入形状为192×256×1
- 输出网格为24×32×8
- XSPI2权重大小、签名和固件常量一致
- 参考输入文件也已替换为49152字节版本

## 256×192迁移顺序

1. 采集并标注原生256×192温度数据。
2. 使用 `dataset_config_256x192_target.json` 作为新配置基础。
3. 训练、导出并在PC端验证int8 TFLite。
4. 使用STM32Cube.AI为STM32N6重新生成网络代码和权重。
5. 替换 `ExtMemLoader/X-CUBE-AI/App/thermal.c/.h`。
6. 更新 `thermal_atonbuf.xSPI2.bin` 及板端权重长度、签名和哈希。
7. 构建Release并先运行固定输入自检。
8. 上板确认NPU输出、检测框方向、推理耗时和长期稳定性。
9. 最后恢复 `CFG_THERMAL_AI_ENABLE`。

## 坐标方向

当前LCD和UART温度流默认使用镜像加翻转方向。板端已有 `tiny1c_thermal_app_transform_frame_point()` 用于坐标变换。训练数据、模型输出和GUI叠加必须保持同一方向约定。
