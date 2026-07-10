# 检测框标注工具使用说明

脚本文件仍然叫：

- [annotate_centerpoints.py](D:/PracticeProject/Stm32/stm32n6_thermal_256x192/thermal_ai/scripts/annotate_centerpoints.py)

但它现在已经不是“中心点标注工具”，而是一个基于 `temp14 .bin` 的多目标、多类别 `bbox` 标注工具。

## 1. 工具作用

它会：

- 读取 `temp14 .bin`
- 按当前 AI 配置生成灰度预览
- 用鼠标拖拽绘制检测框
- 支持一张图里标多个目标、多个类别
- 自动保存同名 `.json`

## 2. 启动方式

### 2.1 最省事的方式：直接从 `thermal_out/` 导入

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out
```

这时工具会自动把样本归档到：

```text
thermal_ai_dataset/raw/<类别>/<自动分组>/
```

从 `thermal_out/` 导入时，工具会复制 `.bin` 和同名附件到 raw 数据集，不会删除 `thermal_out/` 里的原始采集文件。

自动分组默认类似：

- `thermal_out_001`
- `thermal_out_002`
- `thermal_out_003`

并且默认每组最多保存 `12` 张，满了以后会自动切到下一组。

同时，目录模式下工具现在会自动跳过已经有同名 `.json` 标注的样本，尽量只把还没完成的样本留给你。

### 2.2 继续使用固定 session

如果你就是想把这一批全都放进同一个固定 session：

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out --session-name session_20260620_a
```

### 2.3 自己指定自动分组前缀

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out --session-prefix thermal_mix
```

### 2.4 调整每组样本数

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out --session-prefix thermal_mix --auto-samples-per-session 20
```

### 2.5 只手动切组，不自动满组切换

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_out --session-prefix thermal_mix --auto-samples-per-session 0
```

### 2.6 继续标一个已经整理好的现有 session

```powershell
C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe thermal_ai\scripts\annotate_centerpoints.py --input thermal_ai_dataset\raw\circuit_board_normal\session_20260603_a
```

说明：

- 如果这个目录里有一部分样本已经标过，工具会默认跳过它们
- 如果你想专门回看某一张旧标注，直接把那个 `.bin` 文件本身作为 `--input` 传进去即可

## 3. 基本操作

1. 打开工具后会显示当前 `.bin` 的灰度预览图。
2. 按数字键选择当前要画框的类别。
3. 鼠标左键按下并拖拽，松开后生成一个 `bbox`。
4. 一张图里有多个目标，就继续拖多个框。
5. 如果框错了：
   - 鼠标右键删除离点击位置最近的框
   - 或按 `Delete / Backspace` 删除最后一个框
6. 如果当前是混合场景，按 `P` 把“当前选中的框类别”设为这张样本的主归档类别。
7. 标完后按 `S` 或 `Enter` 保存并切到下一张。

## 4. 快捷键

| 按键 | 功能 |
| --- | --- |
| 鼠标左键拖拽 | 画一个框 |
| 鼠标右键 | 删除离点击位置最近的框 |
| `1-2` | 切换类别 |
| `P` | 把当前选中类别设为主归档类别 |
| `S` / `Enter` | 保存并下一张 |
| `G` | 手动切到新分组 |
| `Delete` / `Backspace` | 删除最后一个框 |
| `C` | 清空当前全部框 |
| `Left` / `Right` | 上一张 / 下一张 |
| `Q` / `Esc` | 退出 |

当前默认数字键映射：

1. `circuit_board_normal`
2. `circuit_board_abnormal_hotspot`

## 5. 保存结果

保存后会得到：

```text
thermal_ai_dataset/raw/circuit_board_abnormal_hotspot/thermal_out_001/frame_0001.bin
thermal_ai_dataset/raw/circuit_board_abnormal_hotspot/thermal_out_001/frame_0001.json
```

JSON 示例：

```json
{
  "primary_class_name": "circuit_board_abnormal_hotspot",
  "objects": [
    { "class_name": "circuit_board_abnormal_hotspot", "x_min": 108, "y_min": 40, "x_max": 136, "y_max": 74 }
  ]
}
```

## 6. 混合场景怎么归类

一张图里可以有多个目标，但文件只放到一个主类别目录里。

例如一张图里有异常热点电路板：

```text
thermal_ai_dataset/raw/circuit_board_abnormal_hotspot/thermal_out_001/frame_0001.bin
thermal_ai_dataset/raw/circuit_board_abnormal_hotspot/thermal_out_001/frame_0001.json
```

这时：

- `primary_class_name` 写成 `circuit_board_abnormal_hotspot`
- `objects` 里至少包含一个 `circuit_board_abnormal_hotspot` 框
- 不要把同一帧复制到多个类别目录

## 7. 为什么现在推荐“粗分组”

`split_dataset.py` 在切分训练集、验证集、测试集时，会按分组整体切，不会把同一组拆散。

所以分组的作用主要是：

- 让相邻、相似、同一拍摄阶段的样本尽量留在同一组
- 避免同一段连续样本同时出现在 train 和 val/test

这不要求你精细维护很多复杂 session。

当前更推荐：

- 一小批相邻样本放一组
- 场景明显变化时按 `G` 切一次
- 其余时间让工具自动滚动到下一组

## 8. 方向说明

当前标注工具看到的方向，已经按“和 MCU 默认显示方向一致”的思路收口：

- 你在工具里画的 `bbox`
- 后续推理输出的检测框坐标
- MCU 默认预览方向

现在按同一套方向解释，不再是前后互相反着。
