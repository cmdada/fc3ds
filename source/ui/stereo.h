#pragma once

#include <citro2d.h>
#include <stdbool.h>

#define FC_DEPTH_BG      0.0f
#define FC_DEPTH_CARD    2.0f
#define FC_DEPTH_CONTENT 3.0f
#define FC_DEPTH_FLOAT   5.0f

#define FC_PARALLAX      1.6f

typedef struct {
	float sign;
	float slider;
} FcEye;

typedef struct {
	C3D_RenderTarget *topLeft;
	C3D_RenderTarget *topRight;
	C3D_RenderTarget *bottom;
	float slider;
	bool stereo;

	bool swapEyes;
} FcStereo;

bool fcStereoInit(FcStereo *s);
void fcStereoExit(FcStereo *s);

void fcStereoUpdate(FcStereo *s);

FcEye fcEyeFor(const FcStereo *s, gfx3dSide_t side);

FcEye fcEyeFlat(void);

static inline float fcShift(const FcEye *eye, float depth)
{
	return eye->sign * depth * eye->slider * FC_PARALLAX;
}
