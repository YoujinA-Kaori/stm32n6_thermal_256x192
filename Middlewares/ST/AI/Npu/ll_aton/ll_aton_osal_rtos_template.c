/**
 ******************************************************************************
 * @file    ll_aton_osal_rtos_template.c
 * @brief   Compatibility shim for stale generated build scripts.
 ******************************************************************************
 * @attention
 *
 * The real RTOS template implementation now lives in
 * `ll_aton_osal_rtos_template.inc` and is included directly by the
 * RTOS-specific OSAL translation units.
 *
 * Keep this `.c` file intentionally empty so older generated `subdir.mk`
 * files that still reference `ll_aton_osal_rtos_template.c` can compile
 * without errors until CubeIDE regenerates the build directory.
 ******************************************************************************
 */
