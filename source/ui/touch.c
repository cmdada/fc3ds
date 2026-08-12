#include "ui/touch.h"

#include <3ds.h>
#include <math.h>
#include <string.h>

#define TAP_SLOP 8.0f

#define FRICTION 4.0f

#define MIN_VELOCITY 8.0f

#define RUBBER_BAND 0.35f

void fcTouchInit(FcTouch *t)
{
	memset(t, 0, sizeof *t);
}

void fcTouchUpdate(FcTouch *t)
{
	const u32 held = hidKeysHeld();
	const bool wasDown = t->down;

	t->down     = (held & KEY_TOUCH) != 0;
	t->pressed  = t->down && !wasDown;
	t->released = !t->down && wasDown;

	if (t->down) {
		touchPosition pos;
		hidTouchRead(&pos);

		const float nx = (float)pos.px;
		const float ny = (float)pos.py;

		if (t->pressed) {
			t->dx = t->dy = 0.0f;
			t->startX = nx;
			t->startY = ny;
			t->travel = 0.0f;
			t->heldFrames = 0;
		} else {
			t->dx = nx - t->x;
			t->dy = ny - t->y;
			t->travel += fabsf(t->dx) + fabsf(t->dy);
			t->heldFrames++;
		}

		t->x = nx;
		t->y = ny;
	} else {
		t->dx = t->dy = 0.0f;
		if (t->released)
			t->heldFrames = 0;
	}
}

bool fcTouchInRect(const FcTouch *t, float x, float y, float w, float h)
{
	return t->x >= x && t->x < x + w && t->y >= y && t->y < y + h;
}

bool fcTouchTapped(const FcTouch *t, float x, float y, float w, float h)
{
	if (!t->released || t->travel > TAP_SLOP)
		return false;

	return t->startX >= x && t->startX < x + w &&
	       t->startY >= y && t->startY < y + h;
}

void fcScrollInit(FcScroll *s, float viewHeight)
{
	memset(s, 0, sizeof *s);
	s->viewHeight = viewHeight;
}

float fcScrollMax(const FcScroll *s)
{
	float max = s->contentHeight - s->viewHeight;
	return max > 0.0f ? max : 0.0f;
}

void fcScrollUpdate(FcScroll *s, const FcTouch *t, bool owns, float dt)
{
	const float max = fcScrollMax(s);

	if (dt <= 0.0f)
		dt = 1.0f / 60.0f;

	if (owns && t->pressed) {
		s->dragging   = true;
		s->velocity   = 0.0f;
		s->lastTouchY = t->y;
	}

	if (s->dragging) {
		if (t->down) {
			const float delta = s->lastTouchY - t->y;
			s->lastTouchY = t->y;

			float scale = 1.0f;
			if ((s->offset < 0.0f && delta < 0.0f) ||
			    (s->offset > max && delta > 0.0f))
				scale = RUBBER_BAND;

			s->offset += delta * scale;

			if (dt > 0.0f)
				s->velocity = delta / dt;
		} else {
			s->dragging = false;
		}
	}

	if (!s->dragging) {
		if (fabsf(s->velocity) > MIN_VELOCITY) {
			s->offset += s->velocity * dt;
			s->velocity *= expf(-FRICTION * dt);
		} else {
			s->velocity = 0.0f;
		}

		if (s->offset < 0.0f) {
			s->offset += (0.0f - s->offset) * fminf(1.0f, 12.0f * dt);
			if (fabsf(s->offset) < 0.5f)
				s->offset = 0.0f;
			s->velocity = 0.0f;
		} else if (s->offset > max) {
			s->offset += (max - s->offset) * fminf(1.0f, 12.0f * dt);
			if (fabsf(s->offset - max) < 0.5f)
				s->offset = max;
			s->velocity = 0.0f;
		}
	}
}
