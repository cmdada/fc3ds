#include "ui/stereo.h"

#include <3ds.h>

bool fcStereoInit(FcStereo *s)
{
	gfxSet3D(true);

	s->topLeft  = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	s->topRight = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	s->bottom   = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	s->slider   = 0.0f;
	s->stereo   = false;
	s->swapEyes = false;

	return s->topLeft && s->topRight && s->bottom;
}

void fcStereoExit(FcStereo *s)
{
	s->topLeft = s->topRight = s->bottom = NULL;
	gfxSet3D(false);
}

void fcStereoUpdate(FcStereo *s)
{
	s->slider = osGet3DSliderState();

	s->stereo = s->slider > 0.01f;
}

FcEye fcEyeFor(const FcStereo *s, gfx3dSide_t side)
{
	FcEye eye;
	eye.slider = s->stereo ? s->slider : 0.0f;
	const bool leftEye = (side == GFX_LEFT) != s->swapEyes;
	eye.sign   = leftEye ? 1.0f : -1.0f;
	return eye;
}

FcEye fcEyeFlat(void)
{
	FcEye eye = { 0.0f, 0.0f };
	return eye;
}
