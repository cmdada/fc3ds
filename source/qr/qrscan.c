#include "qr/qrscan.h"

#include "app/log.h"
#include "quirc/quirc.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define STAGE_BYTES (TEX_W * TEX_H * 2)
#define TEX_W 512
#define TEX_H 256

#define FRAME_BYTES (FC_QR_WIDTH * FC_QR_HEIGHT * 2)

#define CAPTURE_TIMEOUT_NS 120000000ULL

#define MAX_CONSECUTIVE_FAILURES 30

#define TEXTURE_TRANSFER_FLAGS                                            \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) |                \
	 GX_TRANSFER_RAW_COPY(0) |                                            \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |                      \
	 GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |                     \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static u16          *s_frame;
static u16          *s_stage;
static C3D_Tex       s_tex;
static Tex3DS_SubTexture s_subtex;
static struct quirc *s_quirc;
static bool          s_camActive;
static bool          s_texReady;
static Handle        s_receiveEvent;
static s16           s_transferUnit;
static int           s_failStreak;
static Handle        s_errorEvent;
static bool          s_receivePending;
static bool          s_captureInterrupted;

static bool cameraStart(FcQrScanner *s)
{
	if (R_FAILED(camInit()))
		return false;

	if (R_FAILED(CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A)) ||
	    R_FAILED(CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A)) ||
	    R_FAILED(CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_30)))
		goto fail;

	CAMU_SetNoiseFilter(SELECT_OUT1, true);
	CAMU_SetAutoExposure(SELECT_OUT1, true);
	CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
	CAMU_SetTrimming(PORT_CAM1, false);

	u32 maxBytes = 0;
	const Result rMax = CAMU_GetMaxBytes(&maxBytes, FC_QR_WIDTH, FC_QR_HEIGHT);
	const Result rSet = CAMU_SetTransferBytes(PORT_CAM1, maxBytes,
	                                          FC_QR_WIDTH, FC_QR_HEIGHT);

	u32 actualUnit = 0;
	CAMU_GetTransferBytes(&actualUnit, PORT_CAM1);

	snprintf(s->diag, sizeof s->diag,
	         "unit %lu/%lu  frame %d  max=%08lX set=%08lX",
	         (unsigned long)actualUnit, (unsigned long)maxBytes,
	         (int)FRAME_BYTES, (unsigned long)rMax, (unsigned long)rSet);

	if (R_FAILED(rMax) || R_FAILED(rSet))
		goto fail;

	if (maxBytes == 0 || maxBytes > 0x7FFF)
		goto fail;
	s_transferUnit = (s16)maxBytes;

	if (R_FAILED(CAMU_Activate(SELECT_OUT1)))
		goto fail;

	if (R_FAILED(CAMU_GetBufferErrorInterruptEvent(&s_errorEvent, PORT_CAM1)))
		s_errorEvent = 0;

	CAMU_ClearBuffer(PORT_CAM1);
	if (R_FAILED(CAMU_StartCapture(PORT_CAM1)))
		goto fail;

	s_camActive = true;
	return true;

fail:
	if (s_errorEvent) {
		svcCloseHandle(s_errorEvent);
		s_errorEvent = 0;
	}
	CAMU_Activate(SELECT_NONE);
	camExit();
	return false;
}

bool fcQrStart(FcQrScanner *s)
{
	memset(s, 0, sizeof *s);
	s->state = FC_QR_STARTING;

	s_transferUnit   = 0;
	s_failStreak     = 0;
	s_errorEvent     = 0;
	s_receivePending = false;
	s_captureInterrupted = false;

	s_frame = malloc(FRAME_BYTES);
	s_stage = linearAlloc(STAGE_BYTES);
	if (!s_frame || !s_stage) {
		snprintf(s->error, sizeof s->error, "out of memory");
		s->state = FC_QR_FAILED;
		goto fail;
	}
	memset(s_frame, 0, FRAME_BYTES);
	memset(s_stage, 0, STAGE_BYTES);

	if (!C3D_TexInit(&s_tex, TEX_W, TEX_H, GPU_RGB565)) {
		snprintf(s->error, sizeof s->error, "cannot allocate preview texture");
		s->state = FC_QR_FAILED;
		goto fail;
	}
	C3D_TexSetFilter(&s_tex, GPU_LINEAR, GPU_NEAREST);
	s_texReady = true;

	GSPGPU_FlushDataCache(s_stage, STAGE_BYTES);
	C3D_SyncDisplayTransfer((u32 *)s_stage, GX_BUFFER_DIM(TEX_W, TEX_H),
	                        (u32 *)s_tex.data, GX_BUFFER_DIM(TEX_W, TEX_H),
	                        TEXTURE_TRANSFER_FLAGS);

	s_subtex.width  = FC_QR_WIDTH;
	s_subtex.height = FC_QR_HEIGHT;
	s_subtex.left   = 0.0f;
	s_subtex.top    = 1.0f;
	s_subtex.right  = (float)FC_QR_WIDTH / TEX_W;
	s_subtex.bottom = 1.0f - (float)FC_QR_HEIGHT / TEX_H;

	s_quirc = quirc_new();
	if (!s_quirc || quirc_resize(s_quirc, FC_QR_WIDTH, FC_QR_HEIGHT) < 0) {
		snprintf(s->error, sizeof s->error, "decoder init failed");
		s->state = FC_QR_FAILED;
		goto fail;
	}

	if (!cameraStart(s)) {
		snprintf(s->error, sizeof s->error,
		         "camera unavailable - try the .cia build");
		s->state = FC_QR_FAILED;
		goto fail;
	}

	s->state = FC_QR_SCANNING;
	FC_LOG("qr: camera started");
	return true;

fail:
	fcQrStop(s);
	return false;
}

void fcQrStop(FcQrScanner *s)
{
	s_transferUnit = 0;

	if (s_receivePending) {
		svcCloseHandle(s_receiveEvent);
		s_receivePending = false;
	}

	if (s_errorEvent) {
		svcCloseHandle(s_errorEvent);
		s_errorEvent = 0;
	}

	if (s_camActive) {
		CAMU_StopCapture(PORT_CAM1);
		CAMU_Activate(SELECT_NONE);
		camExit();
		s_camActive = false;
	}

	if (s_quirc) {
		quirc_destroy(s_quirc);
		s_quirc = NULL;
	}

	if (s_texReady) {
		C3D_TexDelete(&s_tex);
		s_texReady = false;
	}

	if (s_frame) {
		free(s_frame);
		s_frame = NULL;
	}

	if (s_stage) {
		linearFree(s_stage);
		s_stage = NULL;
	}

	if (s && s->state != FC_QR_FOUND && s->state != FC_QR_FAILED)
		s->state = FC_QR_IDLE;
}

static void frameToLuma(const u16 *src, uint8_t *dst, int count)
{
	for (int i = 0; i < count; i++) {
		const u16 p = src[i];
		const int r = ((p >> 11) & 0x1F) << 3;
		const int g = ((p >> 5)  & 0x3F) << 2;
		const int b = (p & 0x1F) << 3;
		dst[i] = (uint8_t)((r * 77 + g * 151 + b * 28) >> 8);
	}
}

static bool payloadLooksUsable(const char *text, int len)
{
	if (len <= 0 || len >= 500)
		return false;
	for (int i = 0; i < len; i++) {
		if ((unsigned char)text[i] < 0x20)
			return false;
	}
	return true;
}

#define WAIT_TIMEOUT_NS 50000000ULL

void fcQrUpdate(FcQrScanner *s)
{
	if (s->state != FC_QR_SCANNING || !s_camActive)
		return;

	if (!s_receivePending) {
		if (R_FAILED(CAMU_SetReceiving(&s_receiveEvent, s_frame, PORT_CAM1,
		                               FRAME_BYTES, s_transferUnit))) {
			if (++s_failStreak > MAX_CONSECUTIVE_FAILURES) {
				snprintf(s->error, sizeof s->error, "camera stopped accepting reads");
				fcQrStop(s);
				s->state = FC_QR_FAILED;
			}
			return;
		}
		s_receivePending = true;
	}

	if (s_captureInterrupted) {
		CAMU_StartCapture(PORT_CAM1);
		s_captureInterrupted = false;
	}

	Handle events[2] = { s_errorEvent, s_receiveEvent };
	const s32 count  = s_errorEvent ? 2 : 1;
	const s32 recvIdx = s_errorEvent ? 1 : 0;
	if (!s_errorEvent)
		events[0] = s_receiveEvent;

	s32 index = -1;
	if (R_FAILED(svcWaitSynchronizationN(&index, events, count, false,
	                                     WAIT_TIMEOUT_NS))) {
		s->waitingFrames++;
		if (s->waitingFrames > 900 && s->framesScanned == 0) {
			snprintf(s->error, sizeof s->error, "no frames from camera");
			fcQrStop(s);
			s->state = FC_QR_FAILED;
		}
		return;
	}

	if (index != recvIdx) {
		svcCloseHandle(s_receiveEvent);
		s_receivePending     = false;
		s_captureInterrupted = true;
		s->bufferErrors++;
		return;
	}

	svcCloseHandle(s_receiveEvent);
	s_receivePending = false;
	s_failStreak = 0;
	s->framesScanned++;

	for (int row = 0; row < FC_QR_HEIGHT; row++) {
		memcpy(s_stage + (size_t)row * TEX_W,
		       s_frame + (size_t)row * FC_QR_WIDTH,
		       FC_QR_WIDTH * 2);
	}
	GSPGPU_FlushDataCache(s_stage, STAGE_BYTES);

	if (s_texReady) {
		C3D_SyncDisplayTransfer((u32 *)s_stage, GX_BUFFER_DIM(TEX_W, TEX_H),
		                        (u32 *)s_tex.data, GX_BUFFER_DIM(TEX_W, TEX_H),
		                        TEXTURE_TRANSFER_FLAGS);
	}

	int w = 0, h = 0;
	uint8_t *luma = quirc_begin(s_quirc, &w, &h);
	if (luma) {
		frameToLuma(s_frame, luma, w * h);
		quirc_end(s_quirc);

		const int found = quirc_count(s_quirc);
		s->sawCode = found > 0;

		for (int i = 0; i < found; i++) {
			struct quirc_code code;
			struct quirc_data data;

			quirc_extract(s_quirc, i, &code);

			const quirc_decode_error_t err = quirc_decode(&code, &data);
			if (err != QUIRC_SUCCESS) {
				snprintf(s->decodeError, sizeof s->decodeError, "%s",
				         quirc_strerror(err));
				continue;
			}

			if (!payloadLooksUsable((const char *)data.payload, data.payload_len)) {
				snprintf(s->decodeError, sizeof s->decodeError,
				         "decoded, but not usable text");
				continue;
			}

			s->decodeError[0] = '\0';

			int len = data.payload_len;
			if (len >= (int)sizeof s->result)
				len = (int)sizeof s->result - 1;

			memcpy(s->result, data.payload, (size_t)len);
			s->result[len] = '\0';
			s->state = FC_QR_FOUND;
			FC_LOG("qr: decoded %d bytes after %d frames", len, s->framesScanned);
			return;
		}
	}
}

C2D_Image fcQrPreview(void)
{
	C2D_Image img = { s_texReady ? &s_tex : NULL, &s_subtex };
	return img;
}
