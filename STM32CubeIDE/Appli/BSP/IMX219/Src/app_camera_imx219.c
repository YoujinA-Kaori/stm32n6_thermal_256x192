#include "app_camera_imx219.h"

#include "i2c.h"
#include "main.h"

#define IMX219_I2C_ADDR_7BIT          0x10U
#define IMX219_I2C_ADDR_HAL           (IMX219_I2C_ADDR_7BIT << 1)
#define IMX219_I2C_TIMEOUT_MS         100U
/* IMX219 background control must not stall interactive Tiny1C commands. If the
 * shared bus is busy, the low-rate camera control loop skips/retries later. */
#define IMX219_I2C_LOCK_WAIT_MS       20U
#define IMX219_CHIP_ID                0x0219U

#define IMX219_REG_CHIP_ID            0x0000U
#define IMX219_REG_SOFTWARE_RESET     0x0103U
#define IMX219_REG_MODE_SELECT        0x0100U
#define IMX219_REG_CSI_LANE_MODE      0x0114U
#define IMX219_REG_DPHY_CTRL          0x0128U
#define IMX219_REG_EXCK_FREQ          0x012AU
#define IMX219_REG_LINE_LENGTH_A      0x0162U
#define IMX219_REG_FRM_LENGTH_A       0x0160U
#define IMX219_REG_X_ADD_STA_A        0x0164U
#define IMX219_REG_X_ADD_END_A        0x0166U
#define IMX219_REG_Y_ADD_STA_A        0x0168U
#define IMX219_REG_Y_ADD_END_A        0x016AU
#define IMX219_REG_X_OUTPUT_SIZE      0x016CU
#define IMX219_REG_Y_OUTPUT_SIZE      0x016EU
#define IMX219_REG_X_ODD_INC_A        0x0170U
#define IMX219_REG_Y_ODD_INC_A        0x0171U
#define IMX219_REG_BINNING_MODE_H     0x0174U
#define IMX219_REG_BINNING_MODE_V     0x0175U
#define IMX219_REG_CSI_DATA_FORMAT_A0 0x018CU
#define IMX219_REG_CSI_DATA_FORMAT_A1 0x018DU
#define IMX219_REG_PREPLLCK_VT_DIV    0x0304U
#define IMX219_REG_PREPLLCK_OP_DIV    0x0305U
#define IMX219_REG_PLL_VT_MPY         0x0306U
#define IMX219_REG_OPPXCK_DIV         0x0309U
#define IMX219_REG_VTPXCK_DIV         0x0301U
#define IMX219_REG_VTSYCK_DIV         0x0303U
#define IMX219_REG_OPSYCK_DIV         0x030BU
#define IMX219_REG_PLL_OP_MPY         0x030CU
#define IMX219_REG_GROUP_HOLD         0x0104U
#define IMX219_REG_ANALOG_GAIN        0x0157U
#define IMX219_REG_DIGITAL_GAIN       0x0158U
#define IMX219_REG_INTEGRATION_TIME   0x015AU
#define IMX219_REG_TEST_PATTERN       0x0600U
#define IMX219_REG_TP_WINDOW_WIDTH    0x0624U
#define IMX219_REG_TP_WINDOW_HEIGHT   0x0626U

#define IMX219_FULL_WIDTH             3280U
#define IMX219_FULL_HEIGHT            2464U
#define IMX219_640X480_LINE_LENGTH    3560U
#define IMX219_640X480_FRAME_LENGTH_15FPS 3412U
#define IMX219_640X480_DEFAULT_EXPOSURE_LINES 0x0D00U
#define IMX219_640X480_DEFAULT_ANALOG_GAIN    0x0080U
#define IMX219_640X480_DEFAULT_DIGITAL_GAIN   0x0100U
#define IMX219_TEST_PATTERN_COLOR_BARS 0x0002U
#define IMX219_POWER_OFF_DELAY_MS      20U
#define IMX219_POWER_ON_DELAY_MS       20U
#define IMX219_RESET_RECOVERY_DELAY_MS 100U

typedef struct
{
  uint16_t reg;
  uint16_t value;
  uint8_t width;
} AppCameraIMX219Reg_t;

volatile uint32_t g_app_camera_imx219_last_reg = 0U;
volatile uint32_t g_app_camera_imx219_last_error = APP_CAMERA_IMX219_OK;
volatile uint32_t g_app_camera_imx219_last_hal_status = HAL_OK;
volatile uint32_t g_app_camera_imx219_transfer_count = 0U;
volatile uint32_t g_app_camera_imx219_readback_status = APP_CAMERA_IMX219_OK;
volatile uint32_t g_app_camera_imx219_readback_lane_mode = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_data_format0 = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_data_format1 = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_test_pattern = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_stream = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_power_cycle_count = 0U;
volatile uint32_t g_app_camera_imx219_reset_retry_count = 0U;
volatile uint32_t g_app_camera_imx219_scan_count = 0U;
volatile uint32_t g_app_camera_imx219_scan_first_ack = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_scan_ack_mask[4] = {0U, 0U, 0U, 0U};
volatile uint32_t g_app_camera_imx219_readback_exck_freq = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_line_length = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_frame_length = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_exposure = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_analog_gain = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_digital_gain = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_x_start = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_x_end = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_y_start = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_y_end = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_x_output = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_y_output = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_binning_h = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_binning_v = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_pll_vt_mpy = 0xffffffffU;
volatile uint32_t g_app_camera_imx219_readback_pll_op_mpy = 0xffffffffU;

static const AppCameraIMX219Reg_t g_imx219_vendor_regs[] =
{
  {0x30EBU, 0x0005U, 1U},
  {0x30EBU, 0x000CU, 1U},
  {0x300AU, 0x00FFU, 1U},
  {0x300BU, 0x00FFU, 1U},
  {0x30EBU, 0x0005U, 1U},
  {0x30EBU, 0x0009U, 1U},
  {0x455EU, 0x0000U, 1U},
  {0x471EU, 0x004BU, 1U},
  {0x4767U, 0x000FU, 1U},
  {0x4750U, 0x0014U, 1U},
  {0x4540U, 0x0000U, 1U},
  {0x47B4U, 0x0014U, 1U},
  {0x4713U, 0x0030U, 1U},
  {0x478BU, 0x0010U, 1U},
  {0x478FU, 0x0010U, 1U},
  {0x4793U, 0x0010U, 1U},
  {0x4797U, 0x000EU, 1U},
  {0x479BU, 0x000EU, 1U},
};

static const AppCameraIMX219Reg_t g_imx219_base_regs[] =
{
  {IMX219_REG_CSI_LANE_MODE,    0x0001U, 1U},
  {IMX219_REG_DPHY_CTRL,        0x0000U, 1U},
  {IMX219_REG_LINE_LENGTH_A,    IMX219_640X480_LINE_LENGTH, 2U},
  {IMX219_REG_X_ODD_INC_A,      0x0001U, 1U},
  {IMX219_REG_Y_ODD_INC_A,      0x0001U, 1U},
  {IMX219_REG_BINNING_MODE_H,   0x0003U, 1U},
  {IMX219_REG_BINNING_MODE_V,   0x0003U, 1U},
  {IMX219_REG_DIGITAL_GAIN,     IMX219_640X480_DEFAULT_DIGITAL_GAIN, 2U},
  {IMX219_REG_ANALOG_GAIN,      IMX219_640X480_DEFAULT_ANALOG_GAIN,  1U},
  {IMX219_REG_INTEGRATION_TIME, IMX219_640X480_DEFAULT_EXPOSURE_LINES, 2U},
};

static const AppCameraIMX219Reg_t g_imx219_raw10_regs[] =
{
  {IMX219_REG_CSI_DATA_FORMAT_A0, 10U, 1U},
  {IMX219_REG_CSI_DATA_FORMAT_A1, 10U, 1U},
};

static const AppCameraIMX219Reg_t g_imx219_fps15_regs[] =
{
  {IMX219_REG_PREPLLCK_VT_DIV, 0x0003U, 1U},
  {IMX219_REG_PREPLLCK_OP_DIV, 0x0003U, 1U},
  {IMX219_REG_VTPXCK_DIV,      5U,      1U},
  {IMX219_REG_VTSYCK_DIV,      1U,      1U},
  {IMX219_REG_OPPXCK_DIV,      10U,     1U},
  {IMX219_REG_OPSYCK_DIV,      1U,      1U},
  {IMX219_REG_PLL_VT_MPY,      57U,     2U},
  {IMX219_REG_PLL_OP_MPY,      114U,    2U},
};

static int32_t IMX219_WriteReg(uint16_t reg, uint16_t value, uint8_t width)
{
  uint8_t data[2];
  HAL_StatusTypeDef hal_status;

  g_app_camera_imx219_last_reg = reg;
  g_app_camera_imx219_last_error = APP_CAMERA_IMX219_OK;
  g_app_camera_imx219_last_hal_status = HAL_OK;
  g_app_camera_imx219_transfer_count++;

  if (app_i2c4_bus_lock_timeout(IMX219_I2C_LOCK_WAIT_MS) != TX_SUCCESS)
  {
    g_app_camera_imx219_last_error = APP_CAMERA_IMX219_ERROR_I2C_LOCK;
    return APP_CAMERA_IMX219_ERROR_I2C_LOCK;
  }

  if (width == 1U)
  {
    data[0] = (uint8_t)value;
    hal_status = HAL_I2C_Mem_Write(&hi2c4, IMX219_I2C_ADDR_HAL, reg, I2C_MEMADD_SIZE_16BIT,
                                   data, 1U, IMX219_I2C_TIMEOUT_MS);
  }
  else
  {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
    hal_status = HAL_I2C_Mem_Write(&hi2c4, IMX219_I2C_ADDR_HAL, reg, I2C_MEMADD_SIZE_16BIT,
                                   data, 2U, IMX219_I2C_TIMEOUT_MS);
  }

  app_i2c4_bus_unlock();

  g_app_camera_imx219_last_hal_status = hal_status;
  if (hal_status != HAL_OK)
  {
    g_app_camera_imx219_last_error = APP_CAMERA_IMX219_ERROR_I2C;
    return APP_CAMERA_IMX219_ERROR_I2C;
  }

  return APP_CAMERA_IMX219_OK;
}

static int32_t IMX219_ReadReg(uint16_t reg, uint8_t width, uint16_t *value)
{
  uint8_t data[2];
  HAL_StatusTypeDef hal_status;

  g_app_camera_imx219_last_reg = reg;
  g_app_camera_imx219_last_error = APP_CAMERA_IMX219_OK;
  g_app_camera_imx219_last_hal_status = HAL_OK;
  g_app_camera_imx219_transfer_count++;

  if ((value == 0) || ((width != 1U) && (width != 2U)))
  {
    g_app_camera_imx219_last_error = APP_CAMERA_IMX219_ERROR_INVALID_ARG;
    return APP_CAMERA_IMX219_ERROR_INVALID_ARG;
  }

  if (app_i2c4_bus_lock_timeout(IMX219_I2C_LOCK_WAIT_MS) != TX_SUCCESS)
  {
    g_app_camera_imx219_last_error = APP_CAMERA_IMX219_ERROR_I2C_LOCK;
    return APP_CAMERA_IMX219_ERROR_I2C_LOCK;
  }

  hal_status = HAL_I2C_Mem_Read(&hi2c4, IMX219_I2C_ADDR_HAL, reg, I2C_MEMADD_SIZE_16BIT,
                                data, width, IMX219_I2C_TIMEOUT_MS);
  app_i2c4_bus_unlock();

  g_app_camera_imx219_last_hal_status = hal_status;
  if (hal_status != HAL_OK)
  {
    g_app_camera_imx219_last_error = APP_CAMERA_IMX219_ERROR_I2C;
    return APP_CAMERA_IMX219_ERROR_I2C;
  }

  if (width == 1U)
  {
    *value = data[0];
  }
  else
  {
    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
  }

  return APP_CAMERA_IMX219_OK;
}

static int32_t IMX219_ReadReg16(uint16_t reg, uint16_t *value)
{
  return IMX219_ReadReg(reg, 2U, value);
}

static int32_t IMX219_WriteTable(const AppCameraIMX219Reg_t *table, uint32_t count)
{
  uint32_t i;
  int32_t status;

  for (i = 0U; i < count; i++)
  {
    status = IMX219_WriteReg(table[i].reg, table[i].value, table[i].width);
    if (status != APP_CAMERA_IMX219_OK)
    {
      return status;
    }
  }

  return APP_CAMERA_IMX219_OK;
}

static int32_t IMX219_WriteCrop(uint16_t width, uint16_t height)
{
  uint16_t x_start = (uint16_t)((IMX219_FULL_WIDTH - width) / 2U);
  uint16_t x_end = (uint16_t)(((IMX219_FULL_WIDTH + width) / 2U) - 1U);
  uint16_t y_start = (uint16_t)((IMX219_FULL_HEIGHT - height) / 2U);
  uint16_t y_end = (uint16_t)(((IMX219_FULL_HEIGHT + height) / 2U) - 1U);
  uint16_t frame_length = (uint16_t)(height + 20U);

  if ((width == 640U) && (height == 480U))
  {
    x_start = 1000U;
    x_end = 2279U;
    y_start = 752U;
    y_end = 1711U;
    frame_length = IMX219_640X480_FRAME_LENGTH_15FPS;
  }

  const AppCameraIMX219Reg_t crop_regs[] =
  {
    {IMX219_REG_X_ADD_STA_A,     x_start,      2U},
    {IMX219_REG_X_ADD_END_A,     x_end,        2U},
    {IMX219_REG_Y_ADD_STA_A,     y_start,      2U},
    {IMX219_REG_Y_ADD_END_A,     y_end,        2U},
    {IMX219_REG_X_OUTPUT_SIZE,   width,        2U},
    {IMX219_REG_Y_OUTPUT_SIZE,   height,       2U},
    {IMX219_REG_FRM_LENGTH_A,    frame_length, 2U},
    {IMX219_REG_TP_WINDOW_WIDTH, width,        2U},
    {IMX219_REG_TP_WINDOW_HEIGHT,height,       2U},
  };

  return IMX219_WriteTable(crop_regs, (uint32_t)(sizeof(crop_regs) / sizeof(crop_regs[0])));
}

static int32_t IMX219_RecordReadback(uint16_t reg, uint8_t width, volatile uint32_t *destination)
{
  uint16_t value = 0U;
  int32_t status;

  status = IMX219_ReadReg(reg, width, &value);
  g_app_camera_imx219_readback_status = (uint32_t)status;
  if (status == APP_CAMERA_IMX219_OK)
  {
    *destination = value;
  }

  return status;
}

static void IMX219_RecordReadbackKeep(uint16_t reg,
                                      uint8_t width,
                                      volatile uint32_t *destination,
                                      int32_t *first_status)
{
  int32_t status = IMX219_RecordReadback(reg, width, destination);

  if ((status != APP_CAMERA_IMX219_OK) &&
      (first_status != 0) &&
      (*first_status == APP_CAMERA_IMX219_OK))
  {
    *first_status = status;
  }
}

static int32_t IMX219_ReadbackSmokeConfig(void)
{
  int32_t critical_status = APP_CAMERA_IMX219_OK;
  int32_t diagnostic_status = APP_CAMERA_IMX219_OK;

  IMX219_RecordReadbackKeep(IMX219_REG_CSI_LANE_MODE, 1U,
                            &g_app_camera_imx219_readback_lane_mode,
                            &critical_status);
  IMX219_RecordReadbackKeep(IMX219_REG_CSI_DATA_FORMAT_A0, 1U,
                            &g_app_camera_imx219_readback_data_format0,
                            &critical_status);
  IMX219_RecordReadbackKeep(IMX219_REG_CSI_DATA_FORMAT_A1, 1U,
                            &g_app_camera_imx219_readback_data_format1,
                            &critical_status);
  IMX219_RecordReadbackKeep(IMX219_REG_TEST_PATTERN, 2U,
                            &g_app_camera_imx219_readback_test_pattern,
                            &critical_status);
  IMX219_RecordReadbackKeep(IMX219_REG_MODE_SELECT, 1U,
                            &g_app_camera_imx219_readback_stream,
                            &critical_status);
  IMX219_RecordReadbackKeep(IMX219_REG_EXCK_FREQ, 2U,
                            &g_app_camera_imx219_readback_exck_freq,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_LINE_LENGTH_A, 2U,
                            &g_app_camera_imx219_readback_line_length,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_FRM_LENGTH_A, 2U,
                            &g_app_camera_imx219_readback_frame_length,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_INTEGRATION_TIME, 2U,
                            &g_app_camera_imx219_readback_exposure,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_ANALOG_GAIN, 1U,
                            &g_app_camera_imx219_readback_analog_gain,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_DIGITAL_GAIN, 2U,
                            &g_app_camera_imx219_readback_digital_gain,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_X_ADD_STA_A, 2U,
                            &g_app_camera_imx219_readback_x_start,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_X_ADD_END_A, 2U,
                            &g_app_camera_imx219_readback_x_end,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_Y_ADD_STA_A, 2U,
                            &g_app_camera_imx219_readback_y_start,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_Y_ADD_END_A, 2U,
                            &g_app_camera_imx219_readback_y_end,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_X_OUTPUT_SIZE, 2U,
                            &g_app_camera_imx219_readback_x_output,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_Y_OUTPUT_SIZE, 2U,
                            &g_app_camera_imx219_readback_y_output,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_BINNING_MODE_H, 1U,
                            &g_app_camera_imx219_readback_binning_h,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_BINNING_MODE_V, 1U,
                            &g_app_camera_imx219_readback_binning_v,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_PLL_VT_MPY, 2U,
                            &g_app_camera_imx219_readback_pll_vt_mpy,
                            &diagnostic_status);
  IMX219_RecordReadbackKeep(IMX219_REG_PLL_OP_MPY, 2U,
                            &g_app_camera_imx219_readback_pll_op_mpy,
                            &diagnostic_status);

  g_app_camera_imx219_readback_status =
      (uint32_t)((critical_status != APP_CAMERA_IMX219_OK) ?
                 critical_status : diagnostic_status);
  return critical_status;
}

static void IMX219_PowerCycle(uint32_t off_delay_ms, uint32_t on_delay_ms)
{
  HAL_GPIO_WritePin(CAM_LED_EN_GPIO_Port, CAM_LED_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(CAM_EN_MODULE_GPIO_Port, CAM_EN_MODULE_Pin, GPIO_PIN_RESET);
  HAL_Delay(off_delay_ms);
  HAL_GPIO_WritePin(CAM_EN_MODULE_GPIO_Port, CAM_EN_MODULE_Pin, GPIO_PIN_SET);
  HAL_Delay(on_delay_ms);
  g_app_camera_imx219_power_cycle_count++;
}

static void IMX219_RecordI2CProbe(void)
{
  const uint32_t address = IMX219_I2C_ADDR_7BIT;

  g_app_camera_imx219_scan_count++;
  g_app_camera_imx219_scan_first_ack = 0xffffffffU;
  g_app_camera_imx219_scan_ack_mask[0] = 0U;
  g_app_camera_imx219_scan_ack_mask[1] = 0U;
  g_app_camera_imx219_scan_ack_mask[2] = 0U;
  g_app_camera_imx219_scan_ack_mask[3] = 0U;

  if (app_i2c4_bus_lock_timeout(IMX219_I2C_LOCK_WAIT_MS) != TX_SUCCESS)
  {
    return;
  }

  if (HAL_I2C_IsDeviceReady(&hi2c4, IMX219_I2C_ADDR_HAL, 1U, 2U) == HAL_OK)
  {
    g_app_camera_imx219_scan_ack_mask[address >> 5] |= (1UL << (address & 0x1FU));
    g_app_camera_imx219_scan_first_ack = address;
  }

  app_i2c4_bus_unlock();
}

int32_t AppCameraIMX219_Init(const AppCameraIMX219Config_t *config, uint16_t *chip_id)
{
  uint16_t id = 0U;
  uint16_t exck;
  int32_t status;

  if ((config == 0) || (chip_id == 0))
  {
    return APP_CAMERA_IMX219_ERROR_INVALID_ARG;
  }

  g_app_camera_imx219_readback_status = APP_CAMERA_IMX219_OK;
  g_app_camera_imx219_readback_lane_mode = 0xffffffffU;
  g_app_camera_imx219_readback_data_format0 = 0xffffffffU;
  g_app_camera_imx219_readback_data_format1 = 0xffffffffU;
  g_app_camera_imx219_readback_test_pattern = 0xffffffffU;
  g_app_camera_imx219_readback_stream = 0xffffffffU;

  IMX219_PowerCycle(IMX219_POWER_OFF_DELAY_MS, IMX219_POWER_ON_DELAY_MS);
  IMX219_RecordI2CProbe();

  status = IMX219_WriteReg(IMX219_REG_SOFTWARE_RESET, 0x0001U, 1U);
  if (status != APP_CAMERA_IMX219_OK)
  {
    g_app_camera_imx219_reset_retry_count++;
    IMX219_PowerCycle(IMX219_RESET_RECOVERY_DELAY_MS, IMX219_POWER_ON_DELAY_MS);
    status = IMX219_WriteReg(IMX219_REG_SOFTWARE_RESET, 0x0001U, 1U);
  }
  if (status != APP_CAMERA_IMX219_OK)
  {
    return status;
  }

  HAL_Delay(6U);

  status = IMX219_ReadReg16(IMX219_REG_CHIP_ID, &id);
  *chip_id = id;
  if (status != APP_CAMERA_IMX219_OK)
  {
    return status;
  }

  if (id != IMX219_CHIP_ID)
  {
    return APP_CAMERA_IMX219_ERROR_CHIP_ID;
  }

  status = IMX219_WriteReg(IMX219_REG_MODE_SELECT, 0x0000U, 1U);
  if (status != APP_CAMERA_IMX219_OK)
  {
    return status;
  }

  exck = (uint16_t)((config->input_clock_hz / 1000000UL) << 8);
  status = IMX219_WriteReg(IMX219_REG_EXCK_FREQ, exck, 2U);
  if (status != APP_CAMERA_IMX219_OK)
  {
    return status;
  }

  status = IMX219_WriteTable(g_imx219_vendor_regs,
                             (uint32_t)(sizeof(g_imx219_vendor_regs) / sizeof(g_imx219_vendor_regs[0])));
  if (status != APP_CAMERA_IMX219_OK)
  {
    return status;
  }

  status = IMX219_WriteTable(g_imx219_base_regs,
                             (uint32_t)(sizeof(g_imx219_base_regs) / sizeof(g_imx219_base_regs[0])));
  if (status != APP_CAMERA_IMX219_OK)
  {
    return status;
  }

  status = IMX219_WriteTable(g_imx219_raw10_regs,
                             (uint32_t)(sizeof(g_imx219_raw10_regs) / sizeof(g_imx219_raw10_regs[0])));
  if (status != APP_CAMERA_IMX219_OK)
  {
    return status;
  }

  status = IMX219_WriteCrop(config->width, config->height);
  if (status != APP_CAMERA_IMX219_OK)
  {
    return status;
  }

  status = IMX219_WriteReg(IMX219_REG_TEST_PATTERN, 0U, 2U);
  if (status != APP_CAMERA_IMX219_OK)
  {
    return status;
  }

  if (config->fps == 15U)
  {
    status = IMX219_WriteTable(g_imx219_fps15_regs,
                               (uint32_t)(sizeof(g_imx219_fps15_regs) / sizeof(g_imx219_fps15_regs[0])));
  }
  else
  {
    status = APP_CAMERA_IMX219_ERROR_INVALID_ARG;
  }

  if (status == APP_CAMERA_IMX219_OK)
  {
    status = IMX219_ReadbackSmokeConfig();
  }

  return status;
}

int32_t AppCameraIMX219_SetStream(uint8_t enable)
{
  int32_t status;

  status = IMX219_WriteReg(IMX219_REG_MODE_SELECT, (enable != 0U) ? 0x0001U : 0x0000U, 1U);
  if (status == APP_CAMERA_IMX219_OK)
  {
    status = IMX219_ReadbackSmokeConfig();
  }

  return status;
}

int32_t AppCameraIMX219_SetTestPattern(uint8_t enable)
{
  uint16_t value = (enable != 0U) ? IMX219_TEST_PATTERN_COLOR_BARS : 0U;
  int32_t status;

  status = IMX219_WriteReg(IMX219_REG_TEST_PATTERN, value, 2U);
  if (status == APP_CAMERA_IMX219_OK)
  {
    status = IMX219_ReadbackSmokeConfig();
  }

  return status;
}

int32_t AppCameraIMX219_UpdateDiagnostics(void)
{
  return IMX219_ReadbackSmokeConfig();
}

int32_t AppCameraIMX219_SetExposure(uint16_t exposure_lines,
                                    uint8_t analog_gain_code,
                                    uint16_t digital_gain_code)
{
  int32_t status;

  /* Grouped-parameter hold makes the three writes land on the same frame
   * boundary, avoiding half-applied exposure flicker while the AE loop is
   * converging. */
  (void)IMX219_WriteReg(IMX219_REG_GROUP_HOLD, 0x0001U, 1U);
  status = IMX219_WriteReg(IMX219_REG_INTEGRATION_TIME, exposure_lines, 2U);
  if (status == APP_CAMERA_IMX219_OK)
  {
    status = IMX219_WriteReg(IMX219_REG_ANALOG_GAIN, analog_gain_code, 1U);
  }
  if (status == APP_CAMERA_IMX219_OK)
  {
    status = IMX219_WriteReg(IMX219_REG_DIGITAL_GAIN, digital_gain_code, 2U);
  }
  (void)IMX219_WriteReg(IMX219_REG_GROUP_HOLD, 0x0000U, 1U);

  if (status == APP_CAMERA_IMX219_OK)
  {
    g_app_camera_imx219_readback_exposure = exposure_lines;
    g_app_camera_imx219_readback_analog_gain = analog_gain_code;
    g_app_camera_imx219_readback_digital_gain = digital_gain_code;
  }

  return status;
}
