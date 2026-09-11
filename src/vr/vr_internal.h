#ifndef VR_INTERNAL_H
#define VR_INTERNAL_H

/* Functions shared between the VR glue files only (not part of the game API). */

#include <openxr/openxr.h>

#ifdef __cplusplus
extern "C" {
#endif

void VrStereoLoadConfig(void);
int  VrMenuRayHit(const XrPosef* aim, int* px, int* py);
void VrInputInit(void);
void VrInputUpdate(void);
void VrPerfInit(void);

#ifdef __cplusplus
}
#endif

#endif
