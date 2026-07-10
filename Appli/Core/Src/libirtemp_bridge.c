#include "libirtemp_bridge.h"

#include "libirtemp.h"

/**
 * @brief Apply libirtemp temperature compensation.
 * @param emissivity Emissivity in the range [0, 1].
 * @param tau_q14 Atmospheric transmittance in Q14 format.
 * @param ambient_temp_c Ambient temperature in degrees Celsius.
 * @param source_temp_c Source temperature in degrees Celsius.
 * @param corrected_temp_c Output compensated temperature in degrees Celsius.
 * @return 0 on success, -1 on parameter or library error.
 */
int32_t libirtemp_bridge_temp_correct(float emissivity,
                                      uint16_t tau_q14,
                                      float ambient_temp_c,
                                      float source_temp_c,
                                      float *corrected_temp_c)
{
    irtemp_error_t ret;

    if (corrected_temp_c == NULL)
    {
        return -1;
    }

    ret = temp_correct(emissivity, tau_q14, ambient_temp_c, source_temp_c, corrected_temp_c);
    if (ret != IRTEMP_SUCCESS)
    {
        return -1;
    }

    return 0;
}
