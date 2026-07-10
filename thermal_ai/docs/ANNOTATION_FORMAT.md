# 热成像检测标注格式

当前使用边界框标注，不再使用中心点标注。标注工具文件名仍为 `annotate_centerpoints.py`，但实际功能已经是bbox编辑器。

## 文件组织

每个温度帧 `.bin` 对应一个同名 `.json`：

```text
thermal_ai_dataset/raw/circuit_board_abnormal_hotspot/session_a/frame_0001.bin
thermal_ai_dataset/raw/circuit_board_abnormal_hotspot/session_a/frame_0001.json
```

划分数据集时，二者必须一起复制到 `processed/`。

## JSON结构

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

| 字段 | 含义 |
|---|---|
| `primary_class_name` | 样本所属的主类别，通常与目录名一致 |
| `objects` | 当前帧中需要检测的目标列表 |
| `class_name` | 单个目标的类别 |
| `x_min`, `y_min` | 左上角坐标 |
| `x_max`, `y_max` | 右下角坐标，训练工具按右下边界处理 |

## 当前160×120数据集坐标约束

- 左上角原点为 `(0,0)`
- `0 <= x_min < x_max <= 160`
- `0 <= y_min < y_max <= 120`
- `.bin` 文件必须为38400字节

项目升级到256×192模型后，新数据集应单独存放，并使用：

- `0 <= x_min < x_max <= 256`
- `0 <= y_min < y_max <= 192`
- `.bin` 文件必须为98304字节

不要在同一个raw目录中混合两种分辨率。

## 标注原则

- 框尽量贴合异常热点区域，避免包含过多背景。
- 同一帧有多个异常热点时应全部标出。
- 正常样本可以没有异常目标框，但目录类别与 `primary_class_name` 必须一致。
- 同一段连续采样应放在同一session，避免相邻帧被拆到训练集和验证集造成数据泄漏。
- 镜像、翻转方向必须与训练和板端叠加方向一致。
