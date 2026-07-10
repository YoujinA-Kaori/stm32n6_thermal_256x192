# Thermal AI 执行命令

所有命令都建议在工程根目录执行：

```powershell
D:\PracticeProject\Stm32\stm32n6_thermal_256x192
```

## 1. 采集原始数据

当前固件默认串口温度流：

- 波特率：`2000000`
- 帧率：约 `5 fps`
- 实时预览尺寸：`128x96`（由256x192温度帧2x2平均降采样）

采集命令：

```powershell
python tools\uart_temp14_parser.py --port COM6 --baud 2000000 --save-bin --save-pgm
```

注意：该命令保存的是128x96实时预览数据，不能直接混入现有160x120训练集，也不能作为原生256x192训练帧。模型迁移前应先补充独立的全分辨率采集方案。

## 2. 启动 bbox 标注工具

### 2.1 直接从 `thermal_out/` 导入，自动粗分组

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out
```

默认行为：

- 自动保存到 `thermal_ai_dataset/raw/<类别>/<自动分组>/`
- 输入来自 `thermal_out/` 时复制样本到 raw 数据集，不删除 `thermal_out/` 原始文件
- 分组名类似 `thermal_out_001`
- 每组默认 `12` 张
- 满组后自动切到下一组
- 已经有同名 `.json` 标注的样本会默认跳过

### 2.2 使用固定 session

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out --session-name session_20260620_a
```

### 2.3 自定义自动分组前缀

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out --session-prefix thermal_mix
```

### 2.4 调整每组张数

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out --session-prefix thermal_mix --auto-samples-per-session 20
```

### 2.5 只手动切组

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out --session-prefix thermal_mix --auto-samples-per-session 0
```

工具内可按 `G` 或点击 `新分组` 手动切到下一组。

### 2.6 打开一个已经存在的 raw session

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_ai_dataset\raw\circuit_board_normal\session_20260603_a
```

如果只是想回看或修改某一张已经标过的样本，最直接的方式是：

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_ai_dataset\raw\circuit_board_normal\session_20260603_a\frame_0001.bin
```

## 3. 划分数据集

```powershell
python thermal_ai\scripts\split_dataset.py --overwrite
```

## 4. 检查标签目录

```powershell
python thermal_ai\scripts\label_check.py --stage raw --root thermal_ai_dataset\raw
python thermal_ai\scripts\label_check.py --stage processed --root thermal_ai_dataset\processed
```

## 5. 检查数据集质量

```powershell
python thermal_ai\scripts\check_dataset.py --split all --json-out thermal_ai\artifacts\reports\dataset_check.json
```

重点检查：

- 类别分布
- 空样本与非空样本是否匹配
- bbox 是否越界
- 网格冲突是否过多

## 6. 查看单样本与归一化效果

```powershell
python thermal_ai\scripts\inspect_samples.py --input thermal_out\frame_00001804.bin
```

如果想顺便导出预览图：

```powershell
python thermal_ai\scripts\inspect_samples.py --input thermal_out\frame_00001804.bin --save-dir thermal_ai\artifacts\reports\inspect
```

## 7. 随机可视化样本

```powershell
python thermal_ai\scripts\visualize_random_samples.py --split train --count 12 --output thermal_ai\artifacts\reports\train_sheet.png
```

## 8. 训练检测模型

```powershell
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 train_cnn.py
```

## 9. 导出 TFLite

```powershell
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 export_tflite.py
```

## 10. 验证 TFLite

```powershell
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 validate_tflite.py --model thermal_ai\artifacts\models\best_model_int8.tflite --keras-model thermal_ai\artifacts\models\best_model.keras
```

## 11. 可视化预测框

```powershell
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 visualize_predictions.py --model thermal_ai\artifacts\models\best_model_int8.tflite --split test --count 12
```

## 12. 生成 CubeAI 验证输入

```powershell
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 export_cubeai_validation_input.py --model thermal_ai\artifacts\models\best_model_int8.tflite --split test --class-name circuit_board_normal --sample-index 0
```

## 13. 当前共享环境

训练、导出、TFLite 验证优先使用环境变量 `THERMAL_AI_PYTHON` 指定的Python，也会尝试工程根目录 `.venv` 和原共享环境：

```text
D:\PracticeProject\thermal_model_tflite\.venv\Scripts\python.exe
```

可移植配置示例：

```powershell
$env:THERMAL_AI_PYTHON = "C:\path\to\tensorflow-venv\Scripts\python.exe"
```

建议：

- 标注工具和不依赖 TensorFlow 的检查脚本，直接用工作区 Python
- 训练、导出、验证，统一走 `run_with_shared_venv.ps1`

## 14. 当前最短闭环

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out
python thermal_ai\scripts\split_dataset.py --overwrite
python thermal_ai\scripts\check_dataset.py --split all
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 train_cnn.py
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 export_tflite.py
powershell -ExecutionPolicy Bypass -File thermal_ai\scripts\run_with_shared_venv.ps1 validate_tflite.py --model thermal_ai\artifacts\models\best_model_int8.tflite --keras-model thermal_ai\artifacts\models\best_model.keras
```
