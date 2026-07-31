#include <string.h>
#if defined(_WIN32) || defined(__MINGW32__)

#include "symbolResolver.hpp"
#include "mathBridge.hpp"

#include <math.h>
#include <cmath>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace MathBridge
{
    void initBridges()
    {
        MAP("atan", bridge_atan);
        MAP("atan2", bridge_atan2);
        MAP("atanf", bridge_atanf);
        MAP("atan2f", bridge_atan2f);
        MAP("tan", bridge_tan);
        MAP("log", bridge_log);
        MAP("logf", bridge_logf);
        MAP("log10", bridge_log10);
        MAP("log10f", bridge_log10f);
        MAP("exp", bridge_exp);
        MAP("expf", bridge_expf);
        MAP("asin", bridge_asin);
        MAP("asinf", bridge_asinf);
        MAP("sin", bridge_sin);
        MAP("sinh", bridge_sinh);
        MAP("sinf", bridge_sinf);
        MAP("sinhf", bridge_sinhf);
        MAP("acos", bridge_acos);
        MAP("acosf", bridge_acosf);
        MAP("cos", bridge_cos);
        MAP("cosh", bridge_cosh);
        MAP("cosf", bridge_cosf);
        MAP("coshf", bridge_coshf);
        MAP("tan", bridge_tan);
        MAP("tanh", bridge_tanh);
        MAP("tanf", bridge_tanf);
        MAP("tanhf", bridge_tanhf);
        MAP("pow", bridge_pow);
        MAP("powf", bridge_powf);
        MAP("hypot", bridge_hypot);
        MAP("hypotf", bridge_hypotf);
        MAP("fmod", bridge_fmod);
        MAP("fmodf", bridge_fmodf);
        MAP("modf", bridge_modf);
        MAP("modff", bridge_modff);
        MAP("sqrt", bridge_sqrt);
        MAP("sqrtf", bridge_sqrtf);
        MAP("div", bridge_div);
        MAP("finitef", bridge_finitef);
        MAP("finite", bridge_finite);
        MAP("frexp", bridge_frexp);
        MAP("frexpf", bridge_frexpf);

        /*
         * GCC normally expands these inline on x86, so a game only emits a
         * real call for the odd spot the optimizer left alone. WMMT3 does that
         * for fabsf and ldexp, and libCg.so imports the glibc-internal
         * __isnanf/__isinff aliases, so the whole C99 set is bridged here
         * rather than one symbol at a time.
         */
        MAP("fabs", bridge_fabs);
        MAP("fabsf", bridge_fabsf);
        MAP("ceil", bridge_ceil);
        MAP("ceilf", bridge_ceilf);
        MAP("floor", bridge_floor);
        MAP("floorf", bridge_floorf);
        MAP("round", bridge_round);
        MAP("roundf", bridge_roundf);
        MAP("trunc", bridge_trunc);
        MAP("truncf", bridge_truncf);
        MAP("rint", bridge_rint);
        MAP("rintf", bridge_rintf);
        MAP("nearbyint", bridge_nearbyint);
        MAP("nearbyintf", bridge_nearbyintf);
        MAP("lrint", bridge_lrint);
        MAP("lround", bridge_lround);
        MAP("ldexp", bridge_ldexp);
        MAP("ldexpf", bridge_ldexpf);
        MAP("scalbn", bridge_ldexp);
        MAP("scalbnf", bridge_ldexpf);
        MAP("cbrt", bridge_cbrt);
        MAP("cbrtf", bridge_cbrtf);
        MAP("exp2", bridge_exp2);
        MAP("exp2f", bridge_exp2f);
        MAP("log2f", bridge_log2f);
        MAP("log1p", bridge_log1p);
        MAP("log1pf", bridge_log1pf);
        MAP("expm1", bridge_expm1);
        MAP("expm1f", bridge_expm1f);
        MAP("asinh", bridge_asinh);
        MAP("asinhf", bridge_asinhf);
        MAP("acosh", bridge_acosh);
        MAP("acoshf", bridge_acoshf);
        MAP("atanh", bridge_atanh);
        MAP("atanhf", bridge_atanhf);
        MAP("copysign", bridge_copysign);
        MAP("copysignf", bridge_copysignf);
        MAP("fmax", bridge_fmax);
        MAP("fmaxf", bridge_fmaxf);
        MAP("fmin", bridge_fmin);
        MAP("fminf", bridge_fminf);
        MAP("fdim", bridge_fdim);
        MAP("fdimf", bridge_fdimf);
        MAP("fma", bridge_fma);
        MAP("fmaf", bridge_fmaf);
        MAP("remainder", bridge_remainder);
        MAP("remainderf", bridge_remainderf);
        MAP("drem", bridge_remainder);
        MAP("dremf", bridge_remainderf);
        MAP("nextafter", bridge_nextafter);
        MAP("nextafterf", bridge_nextafterf);

        // glibc exports the classification helpers under these internal names.
        MAP("__isnan", bridge_isnand);
        MAP("__isnanf", bridge_isnanf);
        MAP("isnanf", bridge_isnanf);
        MAP("__isinf", bridge_isinfd);
        MAP("__isinff", bridge_isinff);
        MAP("isinff", bridge_isinff);
        MAP("__finite", bridge_finite);
        MAP("__finitef", bridge_finitef);
        MAP("__signbit", bridge_signbitd);
        MAP("__signbitf", bridge_signbitf);
        MAP("signbit", bridge_signbitd);
    }
}

extern "C" double bridge_log(double x)
{
    return ::log(x);
}
extern "C" float bridge_logf(float x)
{
    return ::logf(x);
}
extern "C" double bridge_log10(double x)
{
    return ::log10(x);
}
extern "C" float bridge_log10f(float x)
{
    return ::log10f(x);
}
extern "C" double bridge_exp(double x)
{
    return ::exp(x);
}
extern "C" float bridge_expf(float x)
{
    return ::expf(x);
}
extern "C" double bridge_asin(double x)
{
    return ::asin(x);
}
extern "C" float bridge_asinf(float x)
{
    return ::asinf(x);
}
extern "C" double bridge_sin(double x)
{
    return ::sin(x);
}
extern "C" double bridge_sinh(double x)
{
    return ::sinh(x);
}
extern "C" float bridge_sinf(float x)
{
    return ::sinf(x);
}
extern "C" float bridge_sinhf(float x)
{
    return ::sinhf(x);
}
extern "C" double bridge_acos(double x)
{
    return ::acos(x);
}
extern "C" float bridge_acosf(float x)
{
    return ::acosf(x);
}
extern "C" double bridge_cos(double x)
{
    return ::cos(x);
}
extern "C" double bridge_cosh(double x)
{
    return ::cosh(x);
}
extern "C" float bridge_cosf(float x)
{
    return ::cosf(x);
}
extern "C" float bridge_coshf(float x)
{
    return ::coshf(x);
}
extern "C" double bridge_atan(double x)
{
    return ::atan(x);
}
extern "C" double bridge_atan2(double y, double x)
{
    return ::atan2(y, x);
}
extern "C" float bridge_atanf(float x)
{
    return ::atanf(x);
}
extern "C" float bridge_atan2f(float y, float x)
{
    return ::atan2f(y, x);
}
extern "C" double bridge_tan(double x)
{
    return ::tan(x);
}
extern "C" double bridge_tanh(double x)
{
    return ::tanh(x);
}
extern "C" float bridge_tanf(float x)
{
    return ::tanf(x);
}
extern "C" float bridge_tanhf(float x)
{
    return ::tanhf(x);
}
extern "C" double bridge_pow(double x, double y)
{
    return ::pow(x, y);
}
extern "C" float bridge_powf(float x, float y)
{
    return ::powf(x, y);
}
extern "C" double bridge_hypot(double x, double y)
{
    return ::hypot(x, y);
}
extern "C" float bridge_hypotf(float x, float y)
{
    return ::hypotf(x, y);
}
extern "C" double bridge_fmod(double x, double y)
{
    return ::fmod(x, y);
}
extern "C" float bridge_fmodf(float x, float y)
{
    return ::fmodf(x, y);
}
extern "C" double bridge_modf(double x, double *iptr)
{
    return ::modf(x, iptr);
}
extern "C" float bridge_modff(float x, float *iptr)
{
    return ::modff(x, iptr);
}
extern "C" double bridge_sqrt(double x)
{
    return ::sqrt(x);
}
extern "C" float bridge_sqrtf(float x)
{
    return ::sqrtf(x);
}
extern "C" div_t bridge_div(int numerator, int denominator)
{
    return ::div(numerator, denominator);
}
extern "C" int bridge_finite(double x)
{
    return std::isfinite(x) ? 1 : 0;
}
extern "C" int bridge_finitef(float x)
{
    return std::isfinite(x) ? 1 : 0;
}
extern "C" double bridge_frexp(double x, int *exp)
{
    return ::frexp(x, exp);
}
extern "C" float bridge_frexpf(float x, int *exp)
{
    return ::frexpf(x, exp);
}
extern "C" double bridge_fabs(double x)
{
    return ::fabs(x);
}
extern "C" float bridge_fabsf(float x)
{
    return ::fabsf(x);
}
extern "C" double bridge_ceil(double x)
{
    return ::ceil(x);
}
extern "C" float bridge_ceilf(float x)
{
    return ::ceilf(x);
}
extern "C" double bridge_floor(double x)
{
    return ::floor(x);
}
extern "C" float bridge_floorf(float x)
{
    return ::floorf(x);
}
extern "C" double bridge_round(double x)
{
    return ::round(x);
}
extern "C" float bridge_roundf(float x)
{
    return ::roundf(x);
}
extern "C" double bridge_trunc(double x)
{
    return ::trunc(x);
}
extern "C" float bridge_truncf(float x)
{
    return ::truncf(x);
}
extern "C" double bridge_rint(double x)
{
    return ::rint(x);
}
extern "C" float bridge_rintf(float x)
{
    return ::rintf(x);
}
extern "C" double bridge_nearbyint(double x)
{
    return ::nearbyint(x);
}
extern "C" float bridge_nearbyintf(float x)
{
    return ::nearbyintf(x);
}
extern "C" long bridge_lrint(double x)
{
    return ::lrint(x);
}
extern "C" long bridge_lround(double x)
{
    return ::lround(x);
}
extern "C" double bridge_ldexp(double x, int exponent)
{
    return ::ldexp(x, exponent);
}
extern "C" float bridge_ldexpf(float x, int exponent)
{
    return ::ldexpf(x, exponent);
}
extern "C" double bridge_cbrt(double x)
{
    return ::cbrt(x);
}
extern "C" float bridge_cbrtf(float x)
{
    return ::cbrtf(x);
}
extern "C" double bridge_exp2(double x)
{
    return ::exp2(x);
}
extern "C" float bridge_exp2f(float x)
{
    return ::exp2f(x);
}
extern "C" float bridge_log2f(float x)
{
    return ::log2f(x);
}
extern "C" double bridge_log1p(double x)
{
    return ::log1p(x);
}
extern "C" float bridge_log1pf(float x)
{
    return ::log1pf(x);
}
extern "C" double bridge_expm1(double x)
{
    return ::expm1(x);
}
extern "C" float bridge_expm1f(float x)
{
    return ::expm1f(x);
}
extern "C" double bridge_asinh(double x)
{
    return ::asinh(x);
}
extern "C" float bridge_asinhf(float x)
{
    return ::asinhf(x);
}
extern "C" double bridge_acosh(double x)
{
    return ::acosh(x);
}
extern "C" float bridge_acoshf(float x)
{
    return ::acoshf(x);
}
extern "C" double bridge_atanh(double x)
{
    return ::atanh(x);
}
extern "C" float bridge_atanhf(float x)
{
    return ::atanhf(x);
}
extern "C" double bridge_copysign(double x, double y)
{
    return ::copysign(x, y);
}
extern "C" float bridge_copysignf(float x, float y)
{
    return ::copysignf(x, y);
}
extern "C" double bridge_fmax(double x, double y)
{
    return ::fmax(x, y);
}
extern "C" float bridge_fmaxf(float x, float y)
{
    return ::fmaxf(x, y);
}
extern "C" double bridge_fmin(double x, double y)
{
    return ::fmin(x, y);
}
extern "C" float bridge_fminf(float x, float y)
{
    return ::fminf(x, y);
}
extern "C" double bridge_fdim(double x, double y)
{
    return ::fdim(x, y);
}
extern "C" float bridge_fdimf(float x, float y)
{
    return ::fdimf(x, y);
}
extern "C" double bridge_fma(double x, double y, double z)
{
    return ::fma(x, y, z);
}
extern "C" float bridge_fmaf(float x, float y, float z)
{
    return ::fmaf(x, y, z);
}
extern "C" double bridge_remainder(double x, double y)
{
    return ::remainder(x, y);
}
extern "C" float bridge_remainderf(float x, float y)
{
    return ::remainderf(x, y);
}
extern "C" double bridge_nextafter(double x, double y)
{
    return ::nextafter(x, y);
}
extern "C" float bridge_nextafterf(float x, float y)
{
    return ::nextafterf(x, y);
}
extern "C" int bridge_isnand(double x)
{
    return std::isnan(x) ? 1 : 0;
}
extern "C" int bridge_isnanf(float x)
{
    return std::isnan(x) ? 1 : 0;
}
extern "C" int bridge_isinfd(double x)
{
    // glibc's __isinf returns the sign of the infinity, not just a flag.
    return std::isinf(x) ? (x > 0 ? 1 : -1) : 0;
}
extern "C" int bridge_isinff(float x)
{
    return std::isinf(x) ? (x > 0 ? 1 : -1) : 0;
}
extern "C" int bridge_signbitd(double x)
{
    return std::signbit(x) ? 1 : 0;
}
extern "C" int bridge_signbitf(float x)
{
    return std::signbit(x) ? 1 : 0;
}

#endif
