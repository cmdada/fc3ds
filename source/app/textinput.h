/**
 * @file textinput.h
 * @brief Text entry, through whichever keyboard the user has chosen.
 *
 * Two of them, selected in Settings and dispatched here so no caller has to
 * care which is up:
 *
 * - FC_KEYBOARD_SYSTEM is the applet. It takes both screens and suspends the
 *   app until it is dismissed. Anyone who has replaced the on-screen keyboard
 *   on their console (ctr-osk-rt and similar hook the same applet) gets theirs
 *   here without special handling.
 * - FC_KEYBOARD_TOUCH is ctr-osk-rt, linked in from vendor/ and drawn by us.
 *   It owns the bottom screen while the top one keeps showing what is being
 *   typed, so a long string is at least visible as it goes in.
 *
 * Both are modal and both block until the user is done. **Neither may be called
 * from inside a frame** -- the applet abandons it mid-flight, and the touch
 * keyboard opens a frame of its own, which cannot nest. Ask from update(),
 * which the frame loop runs before it opens a frame; every caller here already
 * does, via a want-flag set while drawing.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

struct FcApp;

/**
 * Prompt for a line of text.
 *
 * @param app     Supplies the keyboard preference, and the render state the
 *                touch keyboard draws its top screen with.
 * @param hint    Shown as the keyboard's hint text.
 * @param initial Pre-filled value, may be NULL.
 * @param dst     Receives the result, NUL-terminated.
 * @return false if the user cancelled or the applet is unavailable.
 */
bool fcTextInputAsk(struct FcApp *app, const char *hint, const char *initial,
                      char *dst, size_t dstSize);
