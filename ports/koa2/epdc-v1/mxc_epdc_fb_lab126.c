/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Clean-room v1 implementation of the missing Lab126 EPDC v2 helpers for
 * Kindle Oasis 2 (i.MX7D / MAX77796), included by mxc_epdc_v2_fb.c:792.
 *
 * Scope (ports/koa2/epdc-source-gap.md):
 *  - Implements read_mnt_wfm_v2() (reads /mnt/wfm waveform file and inflates gzip).
 *  - Implements mxc_set_vcom() (range-checked IDME microvolts -> MAX77696_EPD_VID).
 *  - Refuses night-mode voltage/timing helpers (0x04 VDC) without touching
 *    v3p3_regulator, xon_len, or vpdd_len; night-mode waveforms (GCK16/GLKW16)
 *    are rejected at update submission.
 *  - Leaves 0x06 ACD parameters at stock pxp_probe defaults (3 / 21).
 *  - Provides minimal debug/proc helpers (wfm_name_for_mode, paint_string,
 *    mxc_epdc_waveform_proc, proc_entry_cleanup).
 *
 * Host-compiled research implementation; not yet tested on Kindle hardware.
 */

#include <linux/fs.h>
#include <linux/mfd/max77696.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/zlib.h>

#ifndef uV_to_mV
#define uV_to_mV(uV) ((uV) / 1000)
#endif

struct wf_proc_dir_entry {
	const char *name;
	umode_t mode;
	const struct file_operations *f_ops;
	struct proc_dir_entry *proc_entry;
};

#define KOA2_EPDC_WFM_DEFAULT_PATH "/mnt/wfm/waveform_to_use"
#define KOA2_EPDC_WFM_MAX_COMPRESSED (4 * 1024 * 1024)
#define KOA2_EPDC_WFM_MAX_INFLATED (8 * 1024 * 1024)
#define KOA2_EPDC_VCOM_MIN_UV (-5058900)
#define KOA2_EPDC_VCOM_MAX_UV 0
#define KOA2_EPDC_VCOM_STEP_UV 9900

static char *koa2_wfm_path = KOA2_EPDC_WFM_DEFAULT_PATH;
module_param(koa2_wfm_path, charp, 0444);
MODULE_PARM_DESC(koa2_wfm_path, "Path to gzip-compressed EPDC waveform file");

static void *koa2_inflate_gzip(const u8 *in_buf, size_t in_len, size_t *out_len)
{
	struct z_stream_s strm;
	void *workspace = NULL;
	u8 *out_buf = NULL;
	size_t cap = 4 * 1024 * 1024;
	int ret;

	memset(&strm, 0, sizeof(strm));
	workspace = vmalloc(zlib_inflate_workspacesize());
	if (!workspace)
		return NULL;
	out_buf = vmalloc(cap);
	if (!out_buf) {
		vfree(workspace);
		return NULL;
	}

	strm.workspace = workspace;
	strm.next_in = in_buf;
	strm.avail_in = in_len;
	strm.next_out = out_buf;
	strm.avail_out = cap;

	ret = zlib_inflateInit2(&strm, 16 + MAX_WBITS);
	if (ret != Z_OK)
		goto fail;

	while (1) {
		ret = zlib_inflate(&strm, Z_NO_FLUSH);
		if (ret == Z_STREAM_END)
			break;
		if (ret != Z_OK && ret != Z_BUF_ERROR)
			goto fail_end;
		if (strm.avail_out == 0) {
			u8 *bigger;
			size_t new_cap = cap * 2;
			if (new_cap > KOA2_EPDC_WFM_MAX_INFLATED)
				goto fail_end;
			bigger = vmalloc(new_cap);
			if (!bigger)
				goto fail_end;
			memcpy(bigger, out_buf, strm.total_out);
			vfree(out_buf);
			out_buf = bigger;
			strm.next_out = out_buf + strm.total_out;
			strm.avail_out = new_cap - strm.total_out;
			cap = new_cap;
		} else if (strm.avail_in == 0) {
			goto fail_end;
		}
	}

	*out_len = strm.total_out;
	zlib_inflateEnd(&strm);
	vfree(workspace);
	return out_buf;

fail_end:
	zlib_inflateEnd(&strm);
fail:
	vfree(out_buf);
	vfree(workspace);
	return NULL;
}

static void *read_mnt_wfm_v2(struct mxc_epdc_fb_data *fb_data, size_t *wfm_size)
{
	struct file *filp;
	struct inode *inode;
	loff_t fsize;
	u8 *raw = NULL;
	void *result = NULL;
	loff_t pos = 0;
	ssize_t nread = 0;

	if (!fb_data || !wfm_size || !koa2_wfm_path)
		return NULL;
	*wfm_size = 0;

	filp = filp_open(koa2_wfm_path, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		dev_err(fb_data->dev, "read_mnt_wfm_v2: open %s failed (%ld)\n",
			koa2_wfm_path, PTR_ERR(filp));
		return NULL;
	}

	inode = file_inode(filp);
	fsize = i_size_read(inode);
	if (!S_ISREG(inode->i_mode) || fsize < sizeof(struct waveform_data_header) ||
	    fsize > KOA2_EPDC_WFM_MAX_COMPRESSED) {
		dev_err(fb_data->dev, "read_mnt_wfm_v2: invalid file size %lld\n",
			(long long)fsize);
		filp_close(filp, NULL);
		return NULL;
	}

	raw = vmalloc((size_t)fsize);
	if (!raw) {
		filp_close(filp, NULL);
		return NULL;
	}

	while (pos < fsize) {
		nread = kernel_read(filp, pos, (char *)(raw + pos), (size_t)(fsize - pos));
		if (nread <= 0)
			break;
		pos += nread;
	}
	filp_close(filp, NULL);

	if (pos != fsize) {
		dev_err(fb_data->dev, "read_mnt_wfm_v2: short read %lld/%lld\n",
			(long long)pos, (long long)fsize);
		vfree(raw);
		return NULL;
	}

	if (fsize >= 2 && raw[0] == 0x1f && raw[1] == 0x8b) {
		result = koa2_inflate_gzip(raw, (size_t)fsize, wfm_size);
		vfree(raw);
		if (!result || *wfm_size < sizeof(struct waveform_data_header)) {
			dev_err(fb_data->dev, "read_mnt_wfm_v2: gzip inflate failed\n");
			vfree(result);
			*wfm_size = 0;
			return NULL;
		}
		return result;
	}

	*wfm_size = (size_t)fsize;
	return raw;
}

static int mxc_set_vcom(struct mxc_epdc_fb_data *fb_data, int vcom_uV)
{
	int vid, actual_vid, ret;

	if (!fb_data || IS_ERR_OR_NULL(fb_data->vcom_regulator))
		return -ENODEV;

	if (vcom_uV < KOA2_EPDC_VCOM_MIN_UV || vcom_uV > KOA2_EPDC_VCOM_MAX_UV) {
		dev_err(fb_data->dev, "mxc_set_vcom: refusing out-of-range vcom_uV=%d\n",
			vcom_uV);
		return -EINVAL;
	}

	vid = MAX77696_EPD_VID(vcom_uV);
	ret = regulator_set_voltage(fb_data->vcom_regulator,
				    vid, vid + KOA2_EPDC_VCOM_STEP_UV);
	if (ret) {
		dev_err(fb_data->dev, "mxc_set_vcom: set_voltage(%d) failed (%d)\n",
			vcom_uV, ret);
		return ret;
	}

	actual_vid = regulator_get_voltage(fb_data->vcom_regulator);
	dev_info(fb_data->dev, "mxc_set_vcom: vcom_uV=%d -> VCOM=%dmV\n",
		 vcom_uV, MAX77696_EPD_UV(actual_vid) / 1000);
	return 0;
}

static int eink_get_wfm_voltage_control(struct mxc_epdc_fb_data *fb_data,
					struct waveform_data_header *wv_header, u8 *waveform_buf)
{
	(void)wv_header;
	(void)waveform_buf;
	if (fb_data) {
		fb_data->waveform_vcd_buffer = NULL;
		fb_data->waveform_acd_buffer = NULL;
		dev_info(fb_data->dev,
			 "epdc-v1: 0x04/0x06 advanced blocks disabled; night mode blocked\n");
	}
	return -EOPNOTSUPP;
}

static int fetch_epdc_vdc_04_data(struct mxc_epdc_fb_data *fb_data,
				  int mode, int temp_index)
{
	(void)fb_data;
	(void)mode;
	(void)temp_index;
	return -EOPNOTSUPP;
}

static int fetch_epdc_adc_06_data(struct mxc_epdc_fb_data *fb_data,
				  int mode, int temp_index)
{
	(void)fb_data;
	(void)mode;
	(void)temp_index;
	return -EOPNOTSUPP;
}

static const char *wfm_name_for_mode(struct mxc_epdc_fb_data *fb_data, int mode)
{
	(void)fb_data;
	switch (mode) {
	case WAVEFORM_MODE_INIT: return "INIT";
	case WAVEFORM_MODE_DU: return "DU";
	case WAVEFORM_MODE_GC16: return "GC16";
	case WAVEFORM_MODE_GL16: return "GL16";
	case WAVEFORM_MODE_GLR16: return "GLR16";
	case WAVEFORM_MODE_GLD16: return "GLD16";
	case WAVEFORM_MODE_A2: return "A2";
	case WAVEFORM_MODE_GCK16: return "GCK16";
	case WAVEFORM_MODE_GLKW16: return "GLKW16";
	case WAVEFORM_MODE_AUTO: return "AUTO";
	default: return "UNKNOWN";
	}
}

static void paint_string(struct mxc_epdc_fb_data *fb_data,
			 int x, int y, const char *text)
{
	(void)fb_data;
	(void)x;
	(void)y;
	(void)text;
}

static int mxc_epdc_waveform_proc(struct mxc_epdc_fb_data *fb_data)
{
	(void)fb_data;
	return 0;
}

static void proc_entry_cleanup(void)
{
}

#if defined(CONFIG_KERNEL_MODE_NEON)
/* Clean-room scalar equivalent of Lab126's portrait scan-line gradient helper
 * (matches the #else branch of mxc_epdc_v2_fb.c:4000-4012 identically). */
void do_pixel_line(void *pixel_start, uint32_t *strength,
		   uint32_t *transition, uint32_t width)
{
	const uint8_t *px = (const uint8_t *)pixel_start;
	uint32_t col;

	if (!px || !strength || !transition)
		return;
	for (col = 0; col + 1 < width; col += 2) {
		int32_t diff = (int32_t)px[col] - (int32_t)px[col + 1];
		if (diff < 0)
			diff = -diff;
		*strength += (uint32_t)diff;
		if ((uint32_t)diff > 15u) /* ONE_GRAY_LEVEL at mxc_epdc_v2_fb.c:3918 */
			(*transition)++;
	}
}
#endif
