#include <math.h>

/**
 * @brief Hard-float bridge for libirtemp pow helper.
 * @param x Base value.
 * @param y Exponent value.
 * @return pow(x, y) as a double precision result.
 */
double __hardfp_pow(double x, double y)
{
    return pow(x, y);
}

/**
 * @brief Hard-float bridge for libirtemp sqrt helper.
 * @param x Input value.
 * @return sqrt(x) as a double precision result.
 */
double __hardfp_sqrt(double x)
{
    return sqrt(x);
}
