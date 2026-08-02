#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void bridgeGluPerspective(double fovy, double aspect, double nearPlane, double farPlane);
void bridgeGluLookAt(double eyeX, double eyeY, double eyeZ,
                     double centerX, double centerY, double centerZ,
                     double upX, double upY, double upZ);
void bridgeGluOrtho2D(double left, double right, double bottom, double top);
const unsigned char *bridgeGluErrorString(unsigned int error);

#ifdef __cplusplus
}
#endif
