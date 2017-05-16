/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <mfx/mfxvideo.h>
#include <stdlib.h>

#include "libavutil/dict.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavcodec/qsv.h"

#include "ffmpeg.h"

int qsv_buffer_size = 0;

static int qsv_get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream *ist = s->opaque;

    return av_hwframe_get_buffer(ist->hw_frames_ctx, frame, 0);
}

static void qsv_uninit(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    av_buffer_unref(&ist->hw_frames_ctx);
}

static int qsv_device_init(InputStream *ist)
{
    int err;

    err = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_QSV,
                                 ist->hwaccel_device, NULL, 0);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error creating a QSV device\n");
        return err;
    }

    return 0;
}

int qsv_init(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    AVHWFramesContext *frames_ctx;
    AVQSVFramesContext *frames_hwctx;
    mfxVersion ver = { {1, 1} };
    mfxSession session;
    int ret;

    if (!hw_device_ctx) {
        ret = qsv_device_init(ist);
        if (ret < 0)
            return ret;
    }

    /*
     *Query MSDK version. Decoder's behavior is different between
     * version older than 1.19 and 1.19 later.
     */
    ret = MFXInit(MFX_IMPL_AUTO, &ver, &session);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Creating session failed.\n");
        return ret;
    }
    ret = MFXQueryVersion(session, &ver);
    if (ret < 0)
        return ret;
    MFXClose(session);

    if(!ist->hw_frames_ctx) {
        ist->hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
        if (!ist->hw_frames_ctx)
            return AVERROR(ENOMEM);

        frames_ctx   = (AVHWFramesContext*)ist->hw_frames_ctx->data;
        frames_hwctx = frames_ctx->hwctx;

        frames_ctx->width             = s->coded_width;

        //QSV must use full height to allocate surface if the input frame is interlaced
	if(AV_FIELD_PROGRESSIVE == s->field_order)
		frames_ctx->height        = s->coded_height;
	else
		frames_ctx->height        = s->coded_height * 2;

        frames_ctx->format            = AV_PIX_FMT_QSV;
        frames_ctx->sw_format         = s->sw_pix_fmt;
        frames_ctx->initial_pool_size = 0;
        frames_hwctx->frame_type      = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

    if (frames_ctx->initial_pool_size == 0) {
        /*
         *For version older than 1.19, we pre-allocate enough surfaces
         *for decoder; for others, we allocate surfaces dynamically.
         */
        if (ver.Major <= 1 && ver.Minor <= 19)
            frames_ctx->initial_pool_size = 64;
    }

        ret = av_hwframe_ctx_init(ist->hw_frames_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error initializing a QSV frame pool\n");
            return ret;
        }
    }

    ist->hwaccel_get_buffer = qsv_get_buffer;
    ist->hwaccel_uninit     = qsv_uninit;

    return 0;
}
