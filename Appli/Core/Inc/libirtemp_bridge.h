#ifndef LIBIRTEMP_BRIDGE_H
#define LIBIRTEMP_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply libirtemp environment compensation to a temperature sample.
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
                                      float *corrected_temp_c);

#ifdef __cplusplus
}
#endif

#endif /* LIBIRTEMP_BRIDGE_H */
