#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

#define FC_QR_WIDTH  400
#define FC_QR_HEIGHT 240

typedef enum {
	FC_QR_IDLE,
	FC_QR_STARTING,
	FC_QR_SCANNING,
	FC_QR_FOUND,
	FC_QR_FAILED,
} FcQrState;

typedef struct {
	FcQrState state;
	char        result[512];
	char        error[96];
	int         framesScanned;
	int         waitingFrames;
	int         bufferErrors;
	bool        sawCode;

	char        diag[96];
	char        decodeError[48];
} FcQrScanner;

bool fcQrStart(FcQrScanner *s);

void fcQrStop(FcQrScanner *s);

void fcQrUpdate(FcQrScanner *s);

C2D_Image fcQrPreview(void);
