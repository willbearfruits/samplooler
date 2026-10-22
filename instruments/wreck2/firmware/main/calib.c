#include "calib.h"
#include <math.h>
#include <stdlib.h>

static void norm3(float *v) { float m = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (m < 1e-6f) m = 1; v[0] /= m; v[1] /= m; v[2] /= m; }
static int snap(const float *v)
{
    int a = 0;
    for (int i = 1; i < 3; i++) if (fabsf(v[i]) > fabsf(v[a])) a = i;
    return v[a] > 0 ? a + 1 : -(a + 1);
}

// Flat, the accelerometer reads +1 g out of the screen: that is z. Tipping the right edge down adds
// -sin(angle) along screen x: what changed, square to z, points at -x. y is z cross x (towards the bottom
// edge). The board's axes are square to the screen, so each snaps to one IMU axis.
const char *calib_solve(const float flat[3], const float tipped[3], int8_t axis[3])
{
    float u[3] = { flat[0], flat[1], flat[2] };
    norm3(u);
    float d[3] = { tipped[0] - flat[0], tipped[1] - flat[1], tipped[2] - flat[2] };
    float k = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
    float ex[3] = { -(d[0] - k * u[0]), -(d[1] - k * u[1]), -(d[2] - k * u[2]) };
    if (sqrtf(ex[0] * ex[0] + ex[1] * ex[1] + ex[2] * ex[2]) < 0.25f) return "TIP IT FURTHER";
    int cx = snap(ex), cz = snap(u);
    if (abs(cx) == abs(cz)) return "TRY AGAIN";
    float vx[3] = { 0, 0, 0 }, vz[3] = { 0, 0, 0 };
    vx[abs(cx) - 1] = cx > 0 ? 1.0f : -1.0f; vz[abs(cz) - 1] = cz > 0 ? 1.0f : -1.0f;
    float vy[3] = { vx[1] * vz[2] - vx[2] * vz[1], vx[2] * vz[0] - vx[0] * vz[2], vx[0] * vz[1] - vx[1] * vz[0] };
    axis[0] = (int8_t)cx; axis[1] = (int8_t)snap(vy); axis[2] = (int8_t)cz;
    return NULL;
}
