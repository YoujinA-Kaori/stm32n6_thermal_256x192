# STM32N6 热成像目标检测 AI

本目录保存热成像目标检测的标注、训练、TFLite导出和验证工具。模型输入是解包后的单通道 `temp14`，不使用LCD伪彩图、RGB565或YUV图像。

## 当前状态

传感器和固件主链已经迁移到256×192，但AI模型暂不迁移：

- 当前有效模型输入：`160×120×1`
- 当前检测网格：`20×15`
- 每个网格输出：objectness、4个bbox参数和类别logits
- 当前数据集帧大小：38400字节
- 当前CubeAI生成物：`ExtMemLoader/X-CUBE-AI/App/thermal.c/.h`
- 固件AI总开关暂时关闭，避免256×192缓冲与旧模型混用

默认配置 `configs/dataset_config.json` 与现有160×120数据和模型保持一致。`configs/dataset_config_256x192_target.json` 只记录后续迁移目标，当前训练脚本不会默认使用它。

## 检测类别

- `circuit_board_normal`
- `circuit_board_abnormal_hotspot`

当前检测头只对 `circuit_board_abnormal_hotspot` 输出目标框，正常样本主要用于抑制误检。

## 输入语义和归一化

原始数据为小端 `uint16`，每像素语义为 `kelvin × 16`。默认使用相对背景温差归一化：

```text
temp_c = temp14 / 16.0 - 273.15
background_c = percentile(temp_c, 50)
delta_c = temp_c - background_c
normalized = clip((delta_c + 8.0) / 68.0, 0.0, 1.0)
```

异常告警不能只依赖归一化图和模型分类，板端还应结合原始 `temp14`、目标与背景温差及连续帧确认。

## 目录结构

```text
thermal_ai/
  configs/       数据集与检测器配置
  docs/          标注、执行命令和板端接入说明
  scripts/       标注、划分、训练、导出与验证脚本
  src/           共享数据处理和后处理逻辑
  artifacts/     本地生成的模型与报告，默认不提交Git
```

数据集默认位于：

```text
thermal_ai_dataset/
  raw/<class>/<session>/
  processed/{train,val,test}/
```

数据集和训练产物默认不提交Git仓库。

## 标注格式

每个 `.bin` 对应一个同名 `.json`，使用 `bbox xyxy`：

```json
{
  "primary_class_name": "circuit_board_abnormal_hotspot",
  "objects": [
    {
      "class_name": "circuit_board_abnormal_hotspot",
      "x_min": 61,
      "y_min": 64,
      "x_max": 105,
      "y_max": 98
    }
  ]
}
```

详细约束见 `docs/ANNOTATION_FORMAT.md`。

## 常用流程

```powershell
python thermal_ai\scripts\split_dataset.py --overwrite
python thermal_ai\scripts\check_dataset.py --split all
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 train_cnn.py
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 export_tflite.py
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 validate_tflite.py --model thermal_ai\artifacts\models\best_model_int8.tflite --keras-model thermal_ai\artifacts\models\best_model.keras
```

完整命令见 `docs/EXECUTION_COMMANDS.md`。

## 迁移到256×192模型

不能只修改配置常量或CubeAI头文件。迁移必须形成以下闭环：

1. 采集原生256×192 `temp14`，每帧98304字节。
2. 使用256×192坐标重新标注；不要直接把旧数据集当作原生高分辨率数据。
3. 使用目标配置重新划分、训练和验证。
4. 导出并检查TFLite输入为 `[1,192,256,1]`、输出为 `[1,24,32,8]`。
5. 用CubeAI重新生成 `thermal.c/.h` 和权重文件。
6. 同步板端输入/输出长度、阈值、后处理尺寸和XSPI权重哈希。
7. 通过Release构建并上板验证后，再打开固件AI总开关。
