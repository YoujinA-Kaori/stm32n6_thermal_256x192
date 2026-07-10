# Tiny1C 温度补偿公式说明

以下内容基于 `libirtemp.lib` / `libirtemp.h` 的反汇编与头文件整理。

## 1. temp14 单位

库内温度表值单位为 **kelvin * 16**。

换算到摄氏度：

```text
temp_c = raw / 16.0 - 273.15
```

其中 `raw` 为 `uint16_t`。

## 2. 环境补偿温度 `temp_correct()`

接口原型：

```c
irtemp_error_t temp_correct(float ems, uint16_t tau, float ta, float org_temp, float* new_temp);
```

参数含义：

- `ems`: 发射率，范围 `0..1`
- `tau`: 透过率，Q14 定点，范围 `0..16384`
- `ta`: 环境温度，单位 `C`
- `org_temp`: 原始温度，单位 `C`
- `new_temp`: 修正后的温度，单位 `C`

定义：

```text
k = ems * tau / 16384.0
T_org_K = org_temp + 273.15
T_a_K   = ta + 273.15
```

正向补偿：

```text
T_new_K = ((T_org_K^4 - (1 - k) * T_a_K^4) / k)^(1/4)
T_new_C = T_new_K - 273.15
```

反向补偿 `reverse_temp_correct()`：

```text
T_org_K = (k * T_new_K^4 + (1 - k) * T_a_K^4)^(1/4)
T_org_C = T_org_K - 273.15
```

## 3. 透过率 `tau`

### 3.1 `calculate_tau()`

根据你拆库得到的结果，`tau` 量化到 Q14：

```text
tau = min(16384, 16384 * (nuc_dist_high - nuc_dist_low) / (nuc_25_high - nuc_25_low))
```

### 3.2 `read_tau()`

从 `correct_table` 中按以下维度插值：

- 湿度
- 环境温度
- 距离

即：

```text
tau = interpolate(correct_table, humidity, t_env, dist)
```

## 4. NUC / 温度表

### 4.1 `calculate_nuc_with_nuc_factor()`

接口原型：

```c
irtemp_error_t calculate_nuc_with_nuc_factor(NucFactor_t* nuc_factor, float temp, uint16_t* nuc_value);
```

温度先转开尔文：

```text
T_K = temp + 273.15
```

二次模型：

```text
NUC = P0 + (P1 * T_K + P2 * T_K^2) / 2^20
```

其中 `P0/P1/P2` 来自 `NucFactor_t`。

### 4.2 `reverse_calc_NUC_with_nuc_t()`

按 `nuc_table` 反查温度对应的 NUC 值。

### 4.3 `remap_temp()`

按 `nuc_table` 把 NUC 映射回温度表值：

```text
temp_data = kelvin * 16
```

## 5. 当前工程里的实际意义

- 串口 `bin` / `temp14` 数据可以直接用：

```text
temp_c = raw / 16.0 - 273.15
```

- 如果要做**更准的绝对测温**，需要把：
  - 发射率 `ems`
  - 透过率 `tau`
  - 环境温度 `ta`
  - 反射温度 `tu`
  - NUC 表

  一起带入库里的补偿链。

