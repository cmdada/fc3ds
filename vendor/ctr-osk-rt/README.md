# ctr-osk-rt

A touch on-screen keyboard for Nintendo 3DS homebrew.

Horizon's own software keyboard (`swkbdInputText`) is a modal applet: it takes
over both screens, blocks until the user is done, and hands you back a string.
That is the wrong shape for anything interactive. `ctr-osk-rt` is the other shape. It sits on the bottom
screen permanently, never blocks, and hands you one key at a time while your
app keeps running and keeps drawing.

- Five rows of variable-width keys, four layers: lowercase, uppercase, and two symbol layers.
- CTRL modifiers 
- Tab, Esc, Enter and Backspace
- Draws straight to the bottom framebuffer.
- Fully themeable, and you can replace the layout tables

## Building

Needs devkitPro with devkitARM and libctru.

```sh
make                  # lib/libctrosk.a
make example          # example/ctrosk-example.3dsx
sudo -E make install  # into $PORTLIBS, so other projects can just -lctrosk
```

To use it without installing, point your app's `LIBDIRS` at this directory and
add `-lctrosk` to `LIBS` — that is exactly what `example/Makefile` does.

## Quick start

```c
#include <3ds.h>
#include <ctrosk.h>

int main(void) {
  gfxInitDefault();
  consoleInit(GFX_TOP, NULL);

  CtrOsk osk;
  ctrOskInit(&osk);
  osk.title = "my app";

  while (aptMainLoop()) {
    hidScanInput();
    if (hidKeysDown() & KEY_START) break;

    CtrOskEvent ev;
    if (ctrOskUpdate(&osk, &ev) && ev.byte >= 32 && ev.byte <= 126)
      putchar(ev.byte);

    ctrOskDraw(&osk);

    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
  }

  gfxExit();
}
```

If you draw over the bottom screen yourself, call `ctrOskInvalidate` so the
next `ctrOskDraw` repaints rather than assuming it's still intact.

## License

GPL-3.0. See [LICENSE](LICENSE).
