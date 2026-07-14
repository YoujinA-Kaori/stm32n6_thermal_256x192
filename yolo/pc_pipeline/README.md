# 可见光 PCB YOLO PC 闭环

本目录用于完成 STM32N6 可见光摄像头模型的 PC 侧数据整理、训练、评估和 TFLite 量化。当前不包含 CubeAI Studio 转换和板端模型替换。

## 当前模型

- 模型：YOLOv8n Detect
- 输入：`320x320 RGB`
- 类别：9 类常见 PCB 器件
- 训练权重：`yolo/pc_runs/common9_yolov8n_320_w0/weights/best.pt`
- 当前板端候选：`best_full_integer_quant.tflite`

| ID | 类别 |
|---:|---|
| 0 | capacitor |
| 1 | resistor |
| 2 | ic |
| 3 | connector |
| 4 | diode |
| 5 | transistor |
| 6 | led |
| 7 | inductor |
| 8 | switch |

## PC 测试结果

独立测试集包含 122 张图片、3770 个目标。

| 模型 | Precision | Recall | mAP50 | mAP50-95 |
|---|---:|---:|---:|---:|
| PT | 0.7086 | 0.4143 | 0.4658 | 0.2608 |
| Float32 TFLite | 0.6991 | 0.4051 | 0.4598 | 0.2545 |
| Full INT8 PTQ | 0.5549 | 0.3606 | 0.3787 | 0.1805 |
| Per-channel Full INT8 PTQ | 0.5334 | 0.3712 | 0.3715 | 0.1768 |
| 内部 INT8、Float32 I/O | 0.5608 | 0.3602 | 0.3818 | 0.1811 |

PT 转 Float32 TFLite 基本没有明显损失，主要掉点发生在内部激活 PTQ。逐通道权重量化和 Float32 输出都没有恢复精度，因此下一步应优先做 QAT，而不是继续微调普通 PTQ 参数。

## 数据处理原则

- 只纳入同时存在图片和标签文件的样本。
- 原训练集中存在图片但没有标签的 PCB 图不能当作空背景，转换时直接排除。
- 新数据集优先使用 NTFS 硬链接，避免重复占用磁盘空间。
- 标签只保留上述 9 类并重新映射为连续 ID。
- 数据集、虚拟环境、缓存和完整训练目录只保留在本机，不提交 Git。

## 主要脚本

- `prepare_common9_dataset.py`：筛选类别并生成 9 类数据集。
- `train_and_evaluate.py`：训练并完成测试集评估。
- `evaluate_best.py`：重新评估现有 PT 权重。
- `export_tflite_int8.py`：导出默认 Full INT8 TFLite。
- `export_tflite_per_channel.py`：逐通道 PTQ 对照实验。
- `inspect_tflite.py`：检查 TFLite 输入输出和量化参数。
- `validate_tflite.py`：在独立测试集验证 TFLite。
- `predict_contact_sheet.py`：固定抽样生成检测效果总览。

## 典型运行顺序

```powershell
PowerShell -ExecutionPolicy Bypass -File install_gpu_env.ps1
PowerShell -ExecutionPolicy Bypass -File install_tflite_export_env.ps1
python prepare_common9_dataset.py
.\.venv\Scripts\python.exe train_and_evaluate.py
.\.venv\Scripts\python.exe evaluate_best.py
.\.venv\Scripts\python.exe export_tflite_int8.py
.\.venv\Scripts\python.exe validate_tflite.py
```

## STM32N6 结论

当前 Full INT8 模型可以通过 X-CUBE-AI 10.2 / ST Edge AI Core 2.2 的 STM32N6 Neural-ART analyze。静态分析只证明模型可编译、内存可安排，不代表最终板端帧率已经验证。

板端替换前仍需完成：

1. QAT 或量化友好模型优化，使 INT8 精度达到可接受水平。
2. 在 CubeAI Studio 中重新转换最终模型。
3. 重新规划 Neural-ART HyperRAM pool，避免与当前 `.EXTRAM` 从 `0x90000000` 开始的区域重叠。
4. 完成 RGB565 到 `320x320x3 int8` 的融合预处理、后处理和真实上板 benchmark。
