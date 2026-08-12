#include "app/textinput.h"

#include <3ds.h>
#include <string.h>

bool fcTextInputAsk(const char *hint, const char *initial,
                      char *dst, size_t dstSize)
{
	if (!dst || dstSize < 2)
		return false;

	SwkbdState swkbd;
	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int)dstSize - 1);
	swkbdSetHintText(&swkbd, hint ? hint : "");
	swkbdSetValidation(&swkbd, SWKBD_NOTBLANK_NOTEMPTY, 0, 0);

	if (initial && *initial)
		swkbdSetInitialText(&swkbd, initial);

	const SwkbdButton button = swkbdInputText(&swkbd, dst, dstSize);

	if (button != SWKBD_BUTTON_CONFIRM) {
		dst[0] = '\0';
		return false;
	}

	return true;
}
