/* linux/drivers/media/video/exynos/jpeg_hx/jpeg_hx_dev.c
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * http://www.samsung.com/
 *
 * Core file for Samsung Jpeg hx Interface driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/clk.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>

#include <asm/page.h>

#include <mach/irqs.h>
#include <plat/jpeg.h>
#include <plat/cpu.h>
#include <plat/iovmm.h>

#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-core.h>

#include "jpeg_hx_core.h"
#include "jpeg_hx_dev.h"

#include "jpeg_hx_mem.h"
#include "jpeg_hx_regs.h"
#include "regs_jpeg_hx.h"

static int jpeg_hx_dec_queue_setup(struct vb2_queue *vq,
					const struct v4l2_format *fmt, unsigned int *num_buffers,
					unsigned int *num_planes, unsigned int sizes[],
					void *allocators[])
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vq);

	int i;

	if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		*num_planes = ctx->param.dec_param.in_plane;
		for (i = 0; i < ctx->param.dec_param.in_plane; i++) {
			sizes[i] = ctx->param.dec_param.mem_size;
			allocators[i] = ctx->jpeg_dev->alloc_ctx;
		}
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		*num_planes = ctx->param.dec_param.out_plane;
		for (i = 0; i < ctx->param.dec_param.out_plane; i++) {
			sizes[i] = (ctx->param.dec_param.out_width *
				ctx->param.dec_param.out_height *
				ctx->param.dec_param.out_depth[i]) / 8;
			allocators[i] = ctx->jpeg_dev->alloc_ctx;
		}
	}

	return 0;
}

static int jpeg_hx_dec_buf_prepare(struct vb2_buffer *vb)
{
	int i;
	int num_plane = 0;

	struct jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		num_plane = ctx->param.dec_param.in_plane;
		ctx->jpeg_dev->vb2->buf_prepare(vb);
	} else if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		num_plane = ctx->param.dec_param.out_plane;
		ctx->jpeg_dev->vb2->buf_prepare(vb);
	}

	for (i = 0; i < num_plane; i++)
		vb2_set_plane_payload(vb, i, ctx->payload[i]);

	return 0;
}

static int jpeg_hx_dec_buf_finish(struct vb2_buffer *vb)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ctx->jpeg_dev->vb2->buf_finish(vb);
	} else if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ctx->jpeg_dev->vb2->buf_finish(vb);
	}

	return 0;
}

static void jpeg_hx_dec_buf_queue(struct vb2_buffer *vb)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (ctx->m2m_ctx)
		v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}

static int jpeg_hx_enc_queue_setup(struct vb2_queue *vq,
					const struct v4l2_format *fmt, unsigned int *num_buffers,
					unsigned int *num_planes, unsigned int sizes[],
					void *allocators[])
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vq);

	int i;
	if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		*num_planes = ctx->param.enc_param.in_plane;
		for (i = 0; i < ctx->param.enc_param.in_plane; i++) {
			sizes[i] = (ctx->param.enc_param.in_width *
				ctx->param.enc_param.in_height *
				ctx->param.enc_param.in_depth[i]) / 8;
			allocators[i] = ctx->jpeg_dev->alloc_ctx;
		}

	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		*num_planes = ctx->param.enc_param.out_plane;
		for (i = 0; i < ctx->param.enc_param.in_plane; i++) {
			sizes[i] = (ctx->param.enc_param.out_width *
				ctx->param.enc_param.out_height *
				ctx->param.enc_param.out_depth * 2) / 8;
			allocators[i] = ctx->jpeg_dev->alloc_ctx;
		}
	}

	return 0;
}

static int jpeg_hx_enc_buf_prepare(struct vb2_buffer *vb)
{
	int i;
	int num_plane = 0;

	struct jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		num_plane = ctx->param.enc_param.in_plane;
		ctx->jpeg_dev->vb2->buf_prepare(vb);
	} else if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		num_plane = ctx->param.enc_param.out_plane;
		ctx->jpeg_dev->vb2->buf_prepare(vb);
	}

	for (i = 0; i < num_plane; i++)
		vb2_set_plane_payload(vb, i, ctx->payload[i]);

	return 0;
}

static int jpeg_hx_enc_buf_finish(struct vb2_buffer *vb)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ctx->jpeg_dev->vb2->buf_finish(vb);
	} else if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ctx->jpeg_dev->vb2->buf_finish(vb);
	}

	return 0;
}

static void jpeg_hx_enc_buf_queue(struct vb2_buffer *vb)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (ctx->m2m_ctx)
		v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}

static void jpeg_hx_lock(struct vb2_queue *vq)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_lock(&ctx->jpeg_dev->lock);
}

static void jpeg_hx_unlock(struct vb2_queue *vq)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_unlock(&ctx->jpeg_dev->lock);
}

static int jpeg_hx_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	set_bit(CTX_STREAMING, &ctx->flags);

	return 0;
}

static int jpeg_ctx_stop_req(struct jpeg_ctx *ctx)
{
	struct jpeg_ctx *curr_ctx;
	struct jpeg_dev *jpeg = ctx->jpeg_dev;
	int ret = 0;
	unsigned long flags;

	if (jpeg->mode == ENCODING)
		curr_ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev_enc);
	else
		curr_ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev_dec);

	if (!test_bit(CTX_RUN, &ctx->flags) || (curr_ctx != ctx))
		return 0;

	spin_lock_irqsave(&ctx->slock, flags);
	set_bit(CTX_ABORT, &ctx->flags);
	spin_unlock_irqrestore(&ctx->slock, flags);

	ret = wait_event_timeout(jpeg->wait,
			!test_bit(CTX_RUN, &ctx->flags), JPEG_TIMEOUT);
	if (!ret) {
		dev_err(&jpeg->plat_dev->dev, "device failed to stop request\n");
		ret = -EBUSY;
	}

	return ret;
}

static int jpeg_hx_stop_streaming(struct vb2_queue *q)
{
	struct jpeg_ctx *ctx = q->drv_priv;
	struct jpeg_dev *jpeg = ctx->jpeg_dev;
	int ret;

	vb2_wait_for_all_buffers(q);
	ret = jpeg_ctx_stop_req(ctx);
	if (ret < 0)
		dev_err(&jpeg->plat_dev->dev, "wait timeout : %s\n", __func__);

	clear_bit(CTX_STREAMING, &ctx->flags);

	return 0;
}

static struct vb2_ops jpeg_hx_enc_vb2_qops = {
	.queue_setup		= jpeg_hx_enc_queue_setup,
	.buf_prepare		= jpeg_hx_enc_buf_prepare,
	.buf_finish		= jpeg_hx_enc_buf_finish,
	.buf_queue		= jpeg_hx_enc_buf_queue,
	.wait_prepare		= jpeg_hx_unlock,
	.wait_finish		= jpeg_hx_lock,
	.start_streaming	= jpeg_hx_start_streaming,
	.stop_streaming		= jpeg_hx_stop_streaming,
};

static struct vb2_ops jpeg_hx_dec_vb2_qops = {
	.queue_setup		= jpeg_hx_dec_queue_setup,
	.buf_prepare		= jpeg_hx_dec_buf_prepare,
	.buf_finish		= jpeg_hx_dec_buf_finish,
	.buf_queue		= jpeg_hx_dec_buf_queue,
	.wait_prepare		= jpeg_hx_unlock,
	.wait_finish		= jpeg_hx_lock,
	.start_streaming	= jpeg_hx_start_streaming,
	.stop_streaming		= jpeg_hx_stop_streaming,
};

static inline enum jpeg_node_type jpeg_hx_get_node_type(struct file *file)
{
	struct video_device *vdev = video_devdata(file);

	if (!vdev) {
		jpeg_err("failed to get video_device\n");
		return JPEG_NODE_INVALID;
	}

	jpeg_dbg("video_device index: %d\n", vdev->num);

	if (vdev->num == JPEG_HX_NODE_DECODER)
		return JPEG_HX_NODE_DECODER;
	else if (vdev->num == JPEG_HX_NODE_ENCODER)
		return JPEG_HX_NODE_ENCODER;
	else if (vdev->num == JPEG2_HX_NODE_DECODER)
		return JPEG2_HX_NODE_DECODER;
	else if (vdev->num == JPEG2_HX_NODE_ENCODER)
		return JPEG2_HX_NODE_ENCODER;
	else
		return JPEG_NODE_INVALID;
}

static int hx_queue_init_dec(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct jpeg_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &jpeg_hx_dec_vb2_qops;
	src_vq->mem_ops = ctx->jpeg_dev->vb2->ops;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &jpeg_hx_dec_vb2_qops;
	dst_vq->mem_ops = ctx->jpeg_dev->vb2->ops;

	return vb2_queue_init(dst_vq);
}

static int hx_queue_init_enc(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct jpeg_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &jpeg_hx_enc_vb2_qops;
	src_vq->mem_ops = ctx->jpeg_dev->vb2->ops;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &jpeg_hx_enc_vb2_qops;
	dst_vq->mem_ops = ctx->jpeg_dev->vb2->ops;

	return vb2_queue_init(dst_vq);
}

static int jpeg_hx_m2m_open(struct file *file)
{
	struct jpeg_dev *jpeg = video_drvdata(file);
	struct jpeg_ctx *ctx = NULL;
	int ret = 0;
	enum jpeg_node_type node;

	node = jpeg_hx_get_node_type(file);

	if (node == JPEG_NODE_INVALID) {
		jpeg_err("cannot specify node type\n");
		ret = -ENOENT;
		goto err_node_type;
	}

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	file->private_data = ctx;
	ctx->jpeg_dev = jpeg;

	init_waitqueue_head(&jpeg->wait);
	spin_lock_init(&ctx->slock);

	if (node == JPEG_HX_NODE_DECODER || node == JPEG2_HX_NODE_DECODER)
		ctx->m2m_ctx =
			v4l2_m2m_ctx_init(jpeg->m2m_dev_dec, ctx,
				hx_queue_init_dec);
	else
		ctx->m2m_ctx =
			v4l2_m2m_ctx_init(jpeg->m2m_dev_enc, ctx,
				hx_queue_init_enc);

	if (IS_ERR(ctx->m2m_ctx)) {
		int err = PTR_ERR(ctx->m2m_ctx);
		kfree(ctx);
		return err;
	}

	pm_runtime_get_sync(&jpeg->plat_dev->dev);

	return 0;

err_node_type:
	kfree(ctx);
	return ret;
}

static int jpeg_hx_m2m_release(struct file *file)
{
	struct jpeg_ctx *ctx = file->private_data;

	v4l2_m2m_ctx_release(ctx->m2m_ctx);

	pm_runtime_put_sync(&ctx->jpeg_dev->plat_dev->dev);
	kfree(ctx);

	return 0;
}

static unsigned int jpeg_hx_m2m_poll(struct file *file,
				     struct poll_table_struct *wait)
{
	struct jpeg_ctx *ctx = file->private_data;

	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}


static int jpeg_hx_m2m_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct jpeg_ctx *ctx = file->private_data;

	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static const struct v4l2_file_operations jpeg_hx_fops = {
	.owner		= THIS_MODULE,
	.open		= jpeg_hx_m2m_open,
	.release		= jpeg_hx_m2m_release,
	.poll			= jpeg_hx_m2m_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap		= jpeg_hx_m2m_mmap,
};

static void jpeg_hx_device_enc_run(void *priv)
{
	struct jpeg_ctx *ctx = priv;
	struct jpeg_dev *jpeg = ctx->jpeg_dev;
	struct jpeg_enc_param enc_param;
	struct vb2_buffer *vb = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ctx->slock, flags);
	if (test_bit(DEV_RUN, &jpeg->state)) {
		dev_err(&jpeg->plat_dev->dev, "JPEG is already in progress\n");
		goto err_device_run;
	}

	if (test_bit(DEV_SUSPEND, &jpeg->state)) {
		dev_err(&jpeg->plat_dev->dev, "JPEG is in suspend state\n");
		goto err_device_run;
	}

	if (test_bit(CTX_ABORT, &ctx->flags)) {
		dev_err(&jpeg->plat_dev->dev, "aborted JPEG device run\n");
		goto err_device_run;
	}

	jpeg->mode = ENCODING;
	enc_param = ctx->param.enc_param;

	jpeg_hx_sw_reset(jpeg->reg_base);
	jpeg_hx_set_enc_dec_mode(jpeg->reg_base, ENCODING);
	jpeg_hx_set_dma_num(jpeg->reg_base);
	jpeg_hx_clk_on(jpeg->reg_base);
	jpeg_hx_clk_set(jpeg->reg_base, 1);
	jpeg_hx_set_interrupt(jpeg->reg_base);
	jpeg_hx_coef(jpeg->reg_base, ENCODING, jpeg);
	jpeg_hx_set_enc_tbl(jpeg->reg_base, enc_param.quality);
	jpeg_hx_set_encode_tbl_select(jpeg->reg_base, enc_param.quality);
	jpeg_hx_set_stream_size(jpeg->reg_base,
		enc_param.in_width, enc_param.in_height);

	vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	jpeg_hx_set_stream_buf_address(jpeg->reg_base, jpeg->vb2->plane_addr(vb, 0));
	vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	jpeg_hx_set_frame_buf_address(jpeg->reg_base,
	enc_param.in_fmt, jpeg->vb2->plane_addr(vb, 0), enc_param.in_width, enc_param.in_height);

	jpeg_hx_set_enc_out_fmt(jpeg->reg_base, enc_param.out_fmt);
	jpeg_hx_set_enc_in_fmt(jpeg->reg_base, enc_param.in_fmt);
	jpeg_hx_set_enc_luma_stride(jpeg->reg_base, enc_param.in_width, enc_param.in_fmt);
	jpeg_hx_set_enc_cbcr_stride(jpeg->reg_base, enc_param.in_width, enc_param.in_fmt);
	if (enc_param.in_fmt == RGB_565 || ARGB_8888)
		jpeg_hx_set_y16(jpeg->reg_base);

	set_bit(DEV_RUN, &jpeg->state);
	set_bit(CTX_RUN, &ctx->flags);

	jpeg_hx_set_timer(jpeg->reg_base, 0x10000000);
	jpeg_hx_start(jpeg->reg_base);

#ifdef JPEG_PERF
	jpeg->start_time = sched_clock();
#endif
err_device_run:
	spin_unlock_irqrestore(&ctx->slock, flags);
}

static void jpeg_hx_device_dec_run(void *priv)
{
	struct jpeg_ctx *ctx = priv;
	struct jpeg_dev *jpeg = ctx->jpeg_dev;
	struct jpeg_dec_param dec_param;
	struct vb2_buffer *vb = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ctx->slock, flags);
	if (test_bit(DEV_RUN, &jpeg->state)) {
		dev_err(&jpeg->plat_dev->dev, "JPEG is already in progress\n");
		goto err_device_run;
	}

	if (test_bit(DEV_SUSPEND, &jpeg->state)) {
		dev_err(&jpeg->plat_dev->dev, "JPEG is in suspend state\n");
		goto err_device_run;
	}

	if (test_bit(CTX_ABORT, &ctx->flags)) {
		dev_err(&jpeg->plat_dev->dev, "aborted JPEG device run\n");
		goto err_device_run;
	}

	jpeg->mode = DECODING;
	dec_param = ctx->param.dec_param;

	jpeg_hx_sw_reset(jpeg->reg_base);
	jpeg_hx_set_dma_num(jpeg->reg_base);
	jpeg_hx_clk_on(jpeg->reg_base);
	jpeg_hx_clk_set(jpeg->reg_base, 1);
	jpeg_hx_set_dec_out_fmt(jpeg->reg_base, dec_param.out_fmt, 0);
	jpeg_hx_set_enc_dec_mode(jpeg->reg_base, DECODING);
	jpeg_hx_set_dec_bitstream_size(jpeg->reg_base, dec_param.size);
	jpeg_hx_color_mode_select(jpeg->reg_base, dec_param.out_fmt); /* need to check */
	jpeg_hx_set_interrupt(jpeg->reg_base);
	jpeg_hx_set_stream_size(jpeg->reg_base,
		dec_param.out_width, dec_param.out_height);

	vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	jpeg_hx_set_stream_buf_address(jpeg->reg_base, jpeg->vb2->plane_addr(vb, 0));
	vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	jpeg_hx_set_frame_buf_address(jpeg->reg_base,
	dec_param.out_fmt, jpeg->vb2->plane_addr(vb, 0), dec_param.in_width, dec_param.in_height);

	jpeg_hx_set_dec_luma_stride(jpeg->reg_base, dec_param.out_width, dec_param.out_fmt);
	jpeg_hx_set_dec_cbcr_stride(jpeg->reg_base, dec_param.out_width, dec_param.out_fmt);

	if (dec_param.out_width > 0 && dec_param.out_height > 0) {
		if ((dec_param.out_width == dec_param.in_width / 2) &&
			(dec_param.out_height == dec_param.in_height / 2)) {
			jpeg_hx_set_dec_scaling(jpeg->reg_base, JPEG_SCALE_2);
		}
		else if ((dec_param.out_width == dec_param.in_width / 4) &&
			(dec_param.out_height == dec_param.in_height / 4)) {
			jpeg_hx_set_dec_scaling(jpeg->reg_base, JPEG_SCALE_4);
		}
		else if ((dec_param.out_width == dec_param.in_width / 8) &&
			(dec_param.out_height == dec_param.in_height / 8)) {
			jpeg_hx_set_dec_scaling(jpeg->reg_base, JPEG_SCALE_8);
		}
		else {
			jpeg_hx_set_dec_scaling(jpeg->reg_base, JPEG_SCALE_NORMAL);
		}
	}

	jpeg_hx_coef(jpeg->reg_base, DECODING, jpeg);

	set_bit(DEV_RUN, &jpeg->state);
	set_bit(CTX_RUN, &ctx->flags);

	jpeg_hx_set_timer(jpeg->reg_base, 0x10000000);
	jpeg_hx_start(jpeg->reg_base);

#ifdef JPEG_PERF
	jpeg->start_time = sched_clock();
#endif
err_device_run:
	spin_unlock_irqrestore(&ctx->slock, flags);
}

static void jpeg_hx_job_enc_abort(void *priv) {}

static void jpeg_hx_job_dec_abort(void *priv) {}

static struct v4l2_m2m_ops jpeg_hx_m2m_enc_ops = {
	.device_run	= jpeg_hx_device_enc_run,
	.job_abort	= jpeg_hx_job_enc_abort,
};

static struct v4l2_m2m_ops jpeg_hx_m2m_dec_ops = {
	.device_run	= jpeg_hx_device_dec_run,
	.job_abort	= jpeg_hx_job_dec_abort,
};

int jpeg_hx_int_pending(struct jpeg_dev *jpeg)
{
	unsigned int	int_status;

	int_status = jpeg_hx_get_int_status(jpeg->reg_base);
	jpeg_dbg("state(%d)\n", int_status);

	return int_status;
}

static irqreturn_t jpeg_hx_irq(int irq, void *priv)
{
	unsigned int int_status;
	struct vb2_buffer *src_vb, *dst_vb;
	struct jpeg_dev *jpeg = priv;
	struct jpeg_ctx *ctx = NULL;
	unsigned long payload_size = 0;

	int_status = jpeg_hx_get_timer_status(jpeg->reg_base);
	if (int_status & JPEG_TIMER_INT_STAT) {
#ifdef JPEG_PERF
	jpeg->end_time = sched_clock();
	jpeg_dbg("abnormal: %llu\n", jpeg->end_time - jpeg->start_time);
#endif
		dev_err(&jpeg->plat_dev->dev, "%s: time out\n",	__func__);
		printk("dumping registers\n");
		print_hex_dump(KERN_ERR, "", DUMP_PREFIX_ADDRESS, 32, 4, jpeg->reg_base,
			0x0280, false);
		printk("End of JPEG_SFR DUMP\n");
		jpeg_hx_set_timer_clear(jpeg->reg_base);
	} else {
#ifdef JPEG_PERF
	jpeg->end_time = sched_clock();
	jpeg_dbg("nomal: %llu\n", jpeg->end_time - jpeg->start_time);
#endif
	}

	if (jpeg->mode == ENCODING)
		ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev_enc);
	else
		ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev_dec);

	if (ctx == 0) {
		printk(KERN_ERR "ctx is null.\n");
		jpeg_hx_sw_reset(jpeg->reg_base);
		goto ctx_err;
	}

	spin_lock(&ctx->slock);
	int_status = jpeg_hx_int_pending(jpeg);

	jpeg_hx_clear_int_status(jpeg->reg_base, int_status);

	if (int_status == 8 && jpeg->mode == DECODING) {
		jpeg_hx_re_start(jpeg->reg_base);
		spin_unlock(&ctx->slock);
		return IRQ_HANDLED;
	}

	clear_bit(DEV_RUN, &jpeg->state);
	clear_bit(CTX_RUN, &ctx->flags);
	src_vb = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

	if (int_status) {
		switch (int_status & 0xfff) {
		case 0xe20:
			jpeg->irq_ret = OK_ENC_OR_DEC;
			break;
		default:
			jpeg->irq_ret = ERR_UNKNOWN;
			break;
		}
	} else {
		jpeg->irq_ret = ERR_UNKNOWN;
	}

	if (src_vb && dst_vb) {
		if (jpeg->irq_ret == OK_ENC_OR_DEC) {
			if (jpeg->mode == ENCODING) {
				payload_size = jpeg_hx_get_stream_size(jpeg->reg_base);
				vb2_set_plane_payload(dst_vb, 0, payload_size);
				v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
				v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
			} else if (int_status != 8 && jpeg->mode == DECODING) {
				v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
				v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
			}
		} else {
			v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_ERROR);
			v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_ERROR);
			dev_err(&jpeg->plat_dev->dev, "%s: enc/dec error\n", __func__);
		}
		if (test_bit(DEV_SUSPEND, &jpeg->state)) {
			jpeg_dbg("wake up blocked process by suspend\n");
			wake_up(&jpeg->wait);
		} else {
			if (jpeg->mode == ENCODING)
				v4l2_m2m_job_finish(jpeg->m2m_dev_enc, ctx->m2m_ctx);
			else
				v4l2_m2m_job_finish(jpeg->m2m_dev_dec, ctx->m2m_ctx);
		}

		/* Wake up from CTX_ABORT state */
		if (test_and_clear_bit(CTX_ABORT, &ctx->flags))
			wake_up(&jpeg->wait);
	} else {
		dev_err(&jpeg->plat_dev->dev, "failed to get the buffer done\n");
	}

	spin_unlock(&ctx->slock);
ctx_err:
	return IRQ_HANDLED;
}

static int jpeg_hx_probe(struct platform_device *pdev)
{
	struct jpeg_dev *jpeg;
	struct video_device *vfd;
	struct resource *res;
	struct exynos_jpeg_platdata *pdata;
	int ret;

	/* global structure */
	jpeg = devm_kzalloc(&pdev->dev, sizeof(struct jpeg_dev), GFP_KERNEL);
	if (!jpeg) {
		dev_err(&pdev->dev, "%s: not enough memory\n", 	__func__);
		return -ENOMEM;
	}

	jpeg->plat_dev = pdev;
	jpeg->id = pdev->id;
	pdata = pdev->dev.platform_data;
	mutex_init(&jpeg->lock);

	/* Get memory resource and map SFR region. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	jpeg->reg_base = devm_request_and_ioremap(&pdev->dev, res);
	if (jpeg->reg_base == NULL) {
		dev_err(&pdev->dev, "failed to claim register region\n");
		return -ENOENT;
	}

	/* irq */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		jpeg_err("failed to request jpeg irq resource\n");
		return -ENXIO;
	}

	ret = devm_request_irq(&pdev->dev, res->start, jpeg_hx_irq, 0,
			pdev->name, jpeg);
	if (ret) {
		dev_err(&pdev->dev, "failed to install irq\n");
		return ret;
	}

	/* clock */
	jpeg->clk = clk_get(&pdev->dev, pdata->gateclk);
	if (IS_ERR(jpeg->clk)) {
		jpeg_err("failed to find jpeg clock source\n");
		return -ENOENT;
	}

	if (pdata->extra_gateclk) {
		jpeg->sclk_clk = clk_get(&pdev->dev, pdata->extra_gateclk);
		if (IS_ERR(jpeg->sclk_clk)) {
			jpeg_err("failed to find jpeg clock source\n");
			return -ENOENT;
		}
	}

	ret = v4l2_device_register(&pdev->dev, &jpeg->v4l2_dev);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register v4l2 device\n");
		goto err_v4l2;
	}

	/* encoder */
	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto err_vd_alloc_enc;
	}

	vfd->fops = &jpeg_hx_fops;
	vfd->release = video_device_release;
	vfd->ioctl_ops = get_jpeg_hx_enc_v4l2_ioctl_ops();
	vfd->lock = &jpeg->lock;
	snprintf(vfd->name, sizeof(vfd->name), "%s:enc", dev_name(&pdev->dev));
	ret = video_register_device(vfd, VFL_TYPE_GRABBER,
			EXYNOS_VIDEONODE_JPEG_HX_ENC(pdev->id));

	if (ret) {
		v4l2_err(&jpeg->v4l2_dev,
			 "%s(): failed to register video device\n", __func__);
		video_device_release(vfd);
		goto err_vd_alloc_enc;
	}
	v4l2_info(&jpeg->v4l2_dev,
		"JPEG driver is registered to /dev/video%d\n", vfd->num);

	jpeg->vfd_enc = vfd;
	jpeg->m2m_dev_enc = v4l2_m2m_init(&jpeg_hx_m2m_enc_ops);
	if (IS_ERR(jpeg->m2m_dev_enc)) {
		v4l2_err(&jpeg->v4l2_dev,
			"failed to initialize v4l2-m2m device\n");
		ret = PTR_ERR(jpeg->m2m_dev_enc);
		goto err_m2m_init_enc;
	}
	video_set_drvdata(vfd, jpeg);

	/* decoder */
	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto err_vd_alloc_dec;
	}

	vfd->fops = &jpeg_hx_fops;
	vfd->release = video_device_release;
	vfd->ioctl_ops = get_jpeg_hx_dec_v4l2_ioctl_ops();
	vfd->lock = &jpeg->lock;
	snprintf(vfd->name, sizeof(vfd->name), "%s:dec", dev_name(&pdev->dev));
	ret = video_register_device(vfd, VFL_TYPE_GRABBER,
			EXYNOS_VIDEONODE_JPEG_HX_DEC(pdev->id));

	if (ret) {
		v4l2_err(&jpeg->v4l2_dev,
			 "%s(): failed to register video device\n", __func__);
		video_device_release(vfd);
		goto err_vd_alloc_dec;
	}
	v4l2_info(&jpeg->v4l2_dev,
		"JPEG driver is registered to /dev/video%d\n", vfd->num);

	jpeg->vfd_dec = vfd;
	jpeg->m2m_dev_dec = v4l2_m2m_init(&jpeg_hx_m2m_dec_ops);
	if (IS_ERR(jpeg->m2m_dev_dec)) {
		v4l2_err(&jpeg->v4l2_dev,
			"failed to initialize v4l2-m2m device\n");
		ret = PTR_ERR(jpeg->m2m_dev_dec);
		goto err_m2m_init_dec;
	}
	video_set_drvdata(vfd, jpeg);

	platform_set_drvdata(pdev, jpeg);

#ifdef CONFIG_VIDEOBUF2_CMA_PHYS
	jpeg->vb2 = &jpeg_hx_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
	jpeg->vb2 = &jpeg_hx_vb2_ion;
#endif

	jpeg->alloc_ctx = jpeg->vb2->init(jpeg);

	if (IS_ERR(jpeg->alloc_ctx)) {
		ret = PTR_ERR(jpeg->alloc_ctx);
		goto err_video_reg;
	}

	exynos_create_iovmm(&pdev->dev, 3, 3);
	jpeg->vb2->resume(jpeg->alloc_ctx);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);
	jpeg->ver = jpeg_hwget_version(jpeg->reg_base);
	pm_runtime_put_sync(&pdev->dev);
	v4l2_err(&jpeg->v4l2_dev, "jpeg-hx.%d registered successfully\n", jpeg->id);

	return 0;

err_video_reg:
	v4l2_m2m_release(jpeg->m2m_dev_dec);
err_m2m_init_dec:
	video_unregister_device(jpeg->vfd_dec);
	video_device_release(jpeg->vfd_dec);
err_vd_alloc_dec:
	v4l2_m2m_release(jpeg->m2m_dev_enc);
err_m2m_init_enc:
	video_unregister_device(jpeg->vfd_enc);
	video_device_release(jpeg->vfd_enc);
err_vd_alloc_enc:
	v4l2_device_unregister(&jpeg->v4l2_dev);
err_v4l2:
	if (jpeg->sclk_clk)
		clk_put(jpeg->sclk_clk);
	clk_put(jpeg->clk);
	return ret;
}

static int jpeg_hx_remove(struct platform_device *pdev)
{
	struct jpeg_dev *jpeg = platform_get_drvdata(pdev);

	del_timer_sync(&jpeg->watchdog_timer);
	flush_workqueue(jpeg->watchdog_workqueue);
	destroy_workqueue(jpeg->watchdog_workqueue);

	v4l2_m2m_release(jpeg->m2m_dev_enc);
	video_unregister_device(jpeg->vfd_enc);

	v4l2_m2m_release(jpeg->m2m_dev_dec);
	video_unregister_device(jpeg->vfd_dec);

	v4l2_device_unregister(&jpeg->v4l2_dev);

	jpeg->vb2->cleanup(jpeg->alloc_ctx);

	free_irq(jpeg->irq_no, pdev);
	mutex_destroy(&jpeg->lock);
	iounmap(jpeg->reg_base);

	clk_put(jpeg->clk);
	if (jpeg->sclk_clk)
		clk_put(jpeg->sclk_clk);
	kfree(jpeg);

	return 0;
}

int jpeg_set_gate_clk(void)
{
	/* CPLL:666MHz */
	struct clk *aclk_g2d_333, *aclk_g2d_333_nogate;
	unsigned int ret = 0;

	aclk_g2d_333 = clk_get(NULL, "aclk_g2d_333");
	if (IS_ERR(aclk_g2d_333)) {
		pr_err("failed to get %s clock\n", "aclk_g2d_333");
		ret = PTR_ERR(aclk_g2d_333);
		goto err_clk1;
	}

	aclk_g2d_333_nogate = clk_get(NULL, "aclk_g2d_333_nogate");
	if (IS_ERR(aclk_g2d_333_nogate)) {
		pr_err("failed to get %s clock\n", "aclk_g2d_333_nogate");
		ret = PTR_ERR(aclk_g2d_333_nogate);
		goto err_clk2;
	}

	ret = clk_set_parent(aclk_g2d_333_nogate, aclk_g2d_333);
	if (ret)
		goto err_clk3;

err_clk3:
	if (aclk_g2d_333_nogate)
		clk_put(aclk_g2d_333_nogate);
err_clk2:
	if (aclk_g2d_333)
		clk_put(aclk_g2d_333);
err_clk1:
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int jpeg_hx_resume(struct device *dev)
{
	int ret;
	struct jpeg_dev *jpeg = dev_get_drvdata(dev);

	clear_bit(DEV_SUSPEND, &jpeg->state);

	ret = wait_event_timeout(jpeg->wait,
			!test_bit(DEV_RUN, &jpeg->state), JPEG_TIMEOUT);

	if (ret == 0)
		dev_err(&jpeg->plat_dev->dev, "wait timeout\n");

	return 0;
}

static int jpeg_hx_suspend(struct device *dev)
{
	struct jpeg_dev *jpeg = dev_get_drvdata(dev);

	set_bit(DEV_SUSPEND, &jpeg->state);

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int jpeg_hx_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct jpeg_dev *jpeg = platform_get_drvdata(pdev);

	if (jpeg->ctx) {
		clk_disable(jpeg->clk);
		if (jpeg->sclk_clk)
			clk_disable(jpeg->sclk_clk);
	}

#ifdef CONFIG_BUSFREQ_OPP
	/* lock bus frequency */
	dev_unlock(jpeg->bus_dev, &pdev->dev);
#endif
	return 0;
}

static int jpeg_hx_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct jpeg_dev *jpeg = platform_get_drvdata(pdev);
	unsigned int ret = 0;

	if (is_jpeg_ip_5r(dev)) {
		ret = jpeg_set_gate_clk();
		if (ret) {
			printk(KERN_ERR "failed to jpeg_set_gate_clk()\n");
		}
	}

	if (jpeg->ctx) {
		if (jpeg->sclk_clk)
			clk_enable(jpeg->sclk_clk);
		clk_enable(jpeg->clk);
	}

	return 0;
}
#endif

static const struct dev_pm_ops jpeg_hx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(jpeg_hx_suspend, jpeg_hx_resume)
	SET_RUNTIME_PM_OPS(jpeg_hx_runtime_suspend, jpeg_hx_runtime_resume,
			NULL)
};

static struct platform_driver jpeg_hx_driver = {
	.probe		= jpeg_hx_probe,
	.remove		= __devexit_p(jpeg_hx_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= JPEG_HX_NAME,
		.pm = &jpeg_hx_pm_ops,
	},
};
module_platform_driver(jpeg_hx_driver);

MODULE_AUTHOR("taeho07.lee@samsung.com>");
MODULE_DESCRIPTION("JPEG hx H/W Device Driver");
MODULE_LICENSE("GPL");
