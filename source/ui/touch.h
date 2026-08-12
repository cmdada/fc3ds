#pragma once

#include <stdbool.h>

typedef struct {
	bool  down;
	bool  pressed;
	bool  released;
	float x, y;
	float dx, dy;
	float startX, startY;
	float travel;
	int   heldFrames;
} FcTouch;

void fcTouchInit(FcTouch *t);

void fcTouchUpdate(FcTouch *t);

bool fcTouchInRect(const FcTouch *t, float x, float y, float w, float h);

bool fcTouchTapped(const FcTouch *t, float x, float y, float w, float h);

typedef struct {
	float offset;
	float velocity;
	float contentHeight;
	float viewHeight;
	bool  dragging;
	float lastTouchY;
} FcScroll;

void fcScrollInit(FcScroll *s, float viewHeight);

float fcScrollMax(const FcScroll *s);

void fcScrollUpdate(FcScroll *s, const FcTouch *t, bool owns, float dt);
