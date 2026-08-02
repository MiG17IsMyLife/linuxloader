#include "gluBridge.h"

#if defined(PACLOADER_BUILD)

#include <glad/gl.h>

#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum
{
    GLU_INVALID_ENUM_VALUE = 100900,
    GLU_INVALID_VALUE_VALUE = 100901,
    GLU_OUT_OF_MEMORY_VALUE = 100902
};

void bridgeGluPerspective(double fovy, double aspect, double nearPlane, double farPlane)
{
    const double scale = 1.0 / tan(fovy * M_PI / 360.0);
    double matrix[16] = {0};
    matrix[0] = scale / aspect;
    matrix[5] = scale;
    matrix[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    matrix[11] = -1.0;
    matrix[14] = (2.0 * farPlane * nearPlane) / (nearPlane - farPlane);
    glad_glMultMatrixd(matrix);
}

void bridgeGluOrtho2D(double left, double right, double bottom, double top)
{
    glad_glOrtho(left, right, bottom, top, -1.0, 1.0);
}

void bridgeGluLookAt(double eyeX, double eyeY, double eyeZ,
                     double centerX, double centerY, double centerZ,
                     double upX, double upY, double upZ)
{
    double forward[3] = {centerX - eyeX, centerY - eyeY, centerZ - eyeZ};
    double magnitude = sqrt(forward[0] * forward[0] + forward[1] * forward[1] +
                            forward[2] * forward[2]);
    if (magnitude > 0.0)
    {
        forward[0] /= magnitude;
        forward[1] /= magnitude;
        forward[2] /= magnitude;
    }

    double up[3] = {upX, upY, upZ};
    magnitude = sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (magnitude > 0.0)
    {
        up[0] /= magnitude;
        up[1] /= magnitude;
        up[2] /= magnitude;
    }

    double side[3] = {
        forward[1] * up[2] - forward[2] * up[1],
        forward[2] * up[0] - forward[0] * up[2],
        forward[0] * up[1] - forward[1] * up[0]
    };
    magnitude = sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
    if (magnitude > 0.0)
    {
        side[0] /= magnitude;
        side[1] /= magnitude;
        side[2] /= magnitude;
    }

    up[0] = side[1] * forward[2] - side[2] * forward[1];
    up[1] = side[2] * forward[0] - side[0] * forward[2];
    up[2] = side[0] * forward[1] - side[1] * forward[0];

    const double matrix[16] = {
        side[0], up[0], -forward[0], 0.0,
        side[1], up[1], -forward[1], 0.0,
        side[2], up[2], -forward[2], 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    glad_glMultMatrixd(matrix);
    glad_glTranslated(-eyeX, -eyeY, -eyeZ);
}

const unsigned char *bridgeGluErrorString(unsigned int error)
{
    switch (error)
    {
        case GLU_INVALID_ENUM_VALUE: return (const unsigned char *)"GLU_INVALID_ENUM";
        case GLU_INVALID_VALUE_VALUE: return (const unsigned char *)"GLU_INVALID_VALUE";
        case GLU_OUT_OF_MEMORY_VALUE: return (const unsigned char *)"GLU_OUT_OF_MEMORY";
        default:
        {
            static char buffer[64];
            snprintf(buffer, sizeof(buffer), "Unknown GLU error: %u", error);
            return (const unsigned char *)buffer;
        }
    }
}

#endif
