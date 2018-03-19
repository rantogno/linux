/*
 * Copyright Â© 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Madhav Chauhan <madhav.chauhan@intel.com>
 *   Jani Nikula <jani.nikula@intel.com>
 */

#include <drm/drm_mipi_dsi.h>
#include "intel_dsi.h"

static void wait_for_dsi_hdr_credit_release(struct intel_dsi *intel_dsi,
					    enum transcoder dsi_trans)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);

	if (wait_for_us((I915_READ(DSI_CMD_TXCTL(dsi_trans)) &
			FREE_HEADER_CREDIT_MASK) == MAX_HEADER_CREDIT,
			50))
		DRM_ERROR("DSI header credits not released\n");
}

static void wait_for_dsi_payload_credit_release(struct intel_dsi *intel_dsi,
						enum transcoder dsi_trans)
{
	struct drm_i915_private *dev_priv = to_i915(intel_dsi->base.base.dev);

	if (wait_for_us((I915_READ(DSI_CMD_TXCTL(dsi_trans)) &
			FREE_PLOAD_CREDIT_MASK) == MAX_PLOAD_CREDIT,
			50))
		DRM_ERROR("DSI payload credits not released\n");
}

static enum transcoder dsi_port_to_transcoder(enum port port)
{
	if (port == PORT_A)
		return TRANSCODER_DSI_0;
	else
		return TRANSCODER_DSI_1;
}

static void wait_for_cmds_dispatched_to_panel(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct mipi_dsi_device *dsi;
	enum port port;
	enum transcoder dsi_trans;
	int ret;

	/* wait for header/payload credits to be released */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		wait_for_dsi_hdr_credit_release(intel_dsi, dsi_trans);
		wait_for_dsi_payload_credit_release(intel_dsi, dsi_trans);
	}

	/* send nop DCS command */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi = to_mipi_dsi_device(intel_dsi->dsi_hosts[port]->base.dev);
		ret = mipi_dsi_dcs_nop(dsi);
		if (ret < 0)
			DRM_ERROR("error sending DCS NOP command\n");
	}

	/* wait for header credits to be released */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		wait_for_dsi_hdr_credit_release(intel_dsi, dsi_trans);
	}

	/* wait for LP TX in progress bit to be cleared */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		if (wait_for_us(!(I915_READ(DSI_LP_MSG(dsi_trans)) &
				  LPTX_IN_PROGRESS),
				  20))
			DRM_ERROR("LPTX bit not cleared\n");
	}
}

static void dsi_program_swing_and_deemphasis(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;
	int lane;

	for_each_dsi_port(port, intel_dsi->ports) {

		/* Bspec: set scaling mode to 0x6 */
		tmp = I915_READ(ICL_PORT_TX_DW5_LN0(port));
		tmp |= SCALING_MODE_SEL(6);
		I915_WRITE(ICL_PORT_TX_DW5_GRP(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW5_AUX(port));
		tmp |= SCALING_MODE_SEL(6);
		I915_WRITE(ICL_PORT_TX_DW5_AUX(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW5_LN0(port));
		tmp |= TAP2_DISABLE | TAP3_DISABLE;
		I915_WRITE(ICL_PORT_TX_DW5_GRP(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW5_AUX(port));
		tmp |= TAP2_DISABLE | TAP3_DISABLE;
		I915_WRITE(ICL_PORT_TX_DW5_AUX(port), tmp);

		/*
		 * swing and scaling values are taken from DSI
		 * table under vswing programming sequence for
		 * combo phy ddi in BSPEC.
		 * program swing values
		 */
		tmp = I915_READ(ICL_PORT_TX_DW2_LN0(port));
		tmp |= SWING_SEL_UPPER(0x2);
		tmp |= SWING_SEL_LOWER(0x2);
		tmp |= RCOMP_SCALAR(0x98);
		I915_WRITE(ICL_PORT_TX_DW2_GRP(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW2_AUX(port));
		tmp |= SWING_SEL_UPPER(0x2);
		tmp |= SWING_SEL_LOWER(0x2);
		tmp |= RCOMP_SCALAR(0x98);
		I915_WRITE(ICL_PORT_TX_DW2_AUX(port), tmp);

		/* program scaling values */
		tmp = I915_READ(ICL_PORT_TX_DW4_AUX(port));
		tmp |= POST_CURSOR_1(0x0);
		tmp |= POST_CURSOR_2(0x0);
		tmp |= CURSOR_COEFF(0x18);
		I915_WRITE(ICL_PORT_TX_DW4_AUX(port), tmp);

		for (lane = 0; lane <= 3; lane++) {
			/* Bspec: must not use GRP register for write */
			tmp = I915_READ(ICL_PORT_TX_DW4_LN(port, lane));
			tmp |= POST_CURSOR_1(0x0);
			tmp |= POST_CURSOR_2(0x0);
			tmp |= CURSOR_COEFF(0x18);
			I915_WRITE(ICL_PORT_TX_DW4_LN(port, lane), tmp);
		}
	}
}

static void gen11_dsi_program_esc_clk_div(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 bpp = mipi_dsi_pixel_format_to_bpp(intel_dsi->pixel_format);
	u32 afe_clk_khz; /* 8X Clock */
	u32 esc_clk_div_m;

	afe_clk_khz = DIV_ROUND_CLOSEST(intel_dsi->pclk * bpp,
					intel_dsi->lane_count);

	esc_clk_div_m = DIV_ROUND_UP(afe_clk_khz, DSI_MAX_ESC_CLK);

	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(ICL_DSI_ESC_CLK_DIV(port),
				esc_clk_div_m & ICL_ESC_CLK_DIV_MASK);
		POSTING_READ(ICL_DSI_ESC_CLK_DIV(port));
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(ICL_DPHY_ESC_CLK_DIV(port),
				esc_clk_div_m & ICL_ESC_CLK_DIV_MASK);
		POSTING_READ(ICL_DPHY_ESC_CLK_DIV(port));
	}
}

static void gen11_dsi_enable_io_power(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;

	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_DSI_IO_MODECTL(port));
		tmp |= COMBO_PHY_MODE_DSI;
		I915_WRITE(ICL_DSI_IO_MODECTL(port), tmp);
	}

	/*
	 * TODO: Enable IO power using intel_display_power_get()
	 * Currently power domains for ICL are not defined which
	 * are needed by above function.
	 */
}

static void gen11_dsi_power_up_lanes(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;
	u32 lane_mask;

	switch (intel_dsi->lane_count) {
	case 1:
		lane_mask = PWR_DOWN_LN_3_1_0;
		break;
	case 2:
		lane_mask = PWR_DOWN_LN_3_1;
		break;
	case 3:
		lane_mask = PWR_DOWN_LN_3;
		break;
	case 4:
		lane_mask = PWR_UP_ALL_LANES;
		break;
	default:
		lane_mask = PWR_UP_ALL_LANES;
		break;
	}

	lane_mask = lane_mask << PWR_DOWN_LN_SHIFT;

	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_CL_DW10(port));
		tmp &= ~PWR_DOWN_LN_MASK;
		I915_WRITE(ICL_PORT_CL_DW10(port), tmp | lane_mask);
	}
}

static void gen11_dsi_config_phy_lanes_sequence(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;
	int lane;

	/* Step 4b(i) set loadgen select for transmit and aux lanes */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_TX_DW4_AUX(port));
		tmp &= ~LOADGEN_SELECT;
		I915_WRITE(ICL_PORT_TX_DW4_AUX(port), tmp);
		for (lane = 0; lane <= 3; lane++) {
			tmp = I915_READ(ICL_PORT_TX_DW4_LN(port, lane));
			tmp &= ~LOADGEN_SELECT;
			if (lane != 2)
				tmp |= LOADGEN_SELECT;
			I915_WRITE(ICL_PORT_TX_DW4_LN(port, lane), tmp);
		}
	}

	/* Step 4b(ii) set latency optimization for transmit and aux lanes */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_TX_DW2_AUX(port));
		tmp &= ~FRC_LATENCY_OPTIM_MASK;
		tmp |= FRC_LATENCY_OPTIM_VAL(0x5);
		I915_WRITE(ICL_PORT_TX_DW2_AUX(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW2_LN0(port));
		tmp &= ~FRC_LATENCY_OPTIM_MASK;
		tmp |= FRC_LATENCY_OPTIM_VAL(0x5);
		I915_WRITE(ICL_PORT_TX_DW2_GRP(port), tmp);
	}
}

static void gen11_dsi_voltage_swing_program_seq(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 tmp;
	enum port port;

	/* Step C.1:clear common keeper enable bit */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_PCS_DW1_LN0(port));
		tmp &= ~COMMON_KEEPER_EN;
		I915_WRITE(ICL_PORT_PCS_DW1_GRP(port), tmp);
		tmp = I915_READ(ICL_PORT_PCS_DW1_AUX(port));
		tmp &= ~COMMON_KEEPER_EN;
		I915_WRITE(ICL_PORT_PCS_DW1_AUX(port), tmp);
	}

	/*
	 * Step C.3: Set SUS Clock Config bitfield to 11b
	 * Note: Step C.2 (loadgen select program) is done
	 * as part of lane phy sequence configuration
	 */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_CL_DW5(port));
		tmp |= SUS_CLOCK_CONFIG;
		I915_WRITE(ICL_PORT_CL_DW5(port), tmp);
	}

	/* Step C.4: Clear training enable to change swing values */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_TX_DW5_LN0(port));
		tmp &= ~TX_TRAINING_EN;
		I915_WRITE(ICL_PORT_TX_DW5_GRP(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW5_AUX(port));
		tmp &= ~TX_TRAINING_EN;
		I915_WRITE(ICL_PORT_TX_DW5_AUX(port), tmp);
	}

	/* Step C.5: Program swing and de-emphasis */
	dsi_program_swing_and_deemphasis(encoder);

	/* Step: C.6: Set training enable to trigger update */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_PORT_TX_DW5_LN0(port));
		tmp |= TX_TRAINING_EN;
		I915_WRITE(ICL_PORT_TX_DW5_GRP(port), tmp);
		tmp = I915_READ(ICL_PORT_TX_DW5_AUX(port));
		tmp &= ~TX_TRAINING_EN;
		I915_WRITE(ICL_PORT_TX_DW5_AUX(port), tmp);
	}
}

static void gen11_dsi_enable_ddi_buffer(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 tmp;
	enum port port;

	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(DDI_BUF_CTL(port));
		tmp |= DDI_BUF_CTL_ENABLE;
		I915_WRITE(DDI_BUF_CTL(port), tmp);

		if (wait_for_us(!(I915_READ(DDI_BUF_CTL(port)) &
				  DDI_BUF_IS_IDLE),
				  500))
			DRM_ERROR("DDI port:%c buffer idle\n", port_name(port));
	}
}

static void gen11_dsi_setup_dphy_timings(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 tmp;
	enum port port;

	/* Program T-INIT master registers */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_DSI_T_INIT_MASTER(port));
		tmp &= ~MASTER_INIT_TIMER_MASK;
		tmp |= intel_dsi->init_count;
		I915_WRITE(ICL_DSI_T_INIT_MASTER(port), tmp);
	}

	/* Program DPHY clock lanes timings */
	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(DPHY_CLK_TIMING_PARAM(port), intel_dsi->dphy_reg);

		/* shadow register inside display core */
		I915_WRITE(DSI_CLK_TIMING_PARAM(port), intel_dsi->dphy_reg);
	}

	/* Program DPHY data lanes timings */
	for_each_dsi_port(port, intel_dsi->ports) {
		I915_WRITE(DPHY_DATA_TIMING_PARAM(port),
			   intel_dsi->dphy_data_lane_reg);

		/* shadow register inside display core */
		I915_WRITE(DSI_DATA_TIMING_PARAM(port),
			   intel_dsi->dphy_data_lane_reg);
	}

	/*
	 * If DSI link operating at or below an 800 MHz,
	 * TA_SURE should be override and programmed to
	 * a value '0' inside TA_PARAM_REGISTERS otherwise
	 * leave all fields at HW default values.
	 */
	if (intel_dsi->bitrate_khz <= KHz(800)) {
		for_each_dsi_port(port, intel_dsi->ports) {
			tmp = I915_READ(DPHY_TA_TIMING_PARAM(port));
			tmp &= ~TA_SURE_TIME_MASK;
			tmp |= (TA_SURE_OVERRIDE | TA_SURE_TIME(0));
			I915_WRITE(DPHY_TA_TIMING_PARAM(port), tmp);

			/* shadow register inside display core */
			tmp = I915_READ(DSI_TA_TIMING_PARAM(port));
			tmp &= ~TA_SURE_TIME_MASK;
			tmp |= (TA_SURE_OVERRIDE | TA_SURE_TIME(0));
			I915_WRITE(DSI_TA_TIMING_PARAM(port), tmp);
		}
	}
}

static void gen11_dsi_configure_transcoder(struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct intel_crtc *intel_crtc = to_intel_crtc(pipe_config->base.crtc);
	enum pipe pipe = intel_crtc->pipe;
	u32 tmp;
	enum port port;
	enum transcoder dsi_trans;

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = I915_READ(DSI_TRANS_FUNC_CONF(dsi_trans));

		if (intel_dsi->eotp_pkt == 0)
			tmp |= EOTP_DISABLED;
		else
			tmp &= ~EOTP_DISABLED;

		/* enable link calibration if freq < 1.5Gbps */
		if (intel_dsi->bitrate_khz <= (1500 * 1000)) {
			tmp &= ~LINK_CALIBRATION_MASK;
			tmp |= LINK_CALIBRATION(
					CALIBRATION_ENABLED_INITIAL_ONLY);
		}

		/* configure continiuos clock */
		tmp &= ~CONTINUOUS_CLK_MASK;
		tmp |= CONTINUOUS_CLK(CLK_ENTER_LP_AFTER_DATA);

		/* configure buffer threshold limit to minimum */
		tmp &= ~PIX_BUF_THRESHOLD_MASK;
		tmp |= PIX_BUF_THRESHOLD(PIX_BUF_THRESHOLD_1_4);

		/* set virtual channel to '0' */
		tmp &= ~PIX_VIRT_CHAN_MASK;
		tmp |= PIX_VIRT_CHAN(0x0);

		/* program BGR transmission */
		if (intel_dsi->bgr_enabled == 0)
			tmp |= BGR_TRANSMISSION;
		else
			tmp &= ~BGR_TRANSMISSION;

		/* select pixel format */
		tmp &= ~PIX_FMT_MASK;

		switch (intel_dsi->pixel_format) {
		case MIPI_DSI_FMT_RGB888:
			tmp |= PIX_FMT(PIX_FMT_RGB888);
			break;
		case MIPI_DSI_FMT_RGB666:
			tmp |= PIX_FMT(PIX_FMT_RGB666_LOOSE);
			break;
		case MIPI_DSI_FMT_RGB666_PACKED:
			tmp |= PIX_FMT(PIX_FMT_RGB666_PACKED);
			break;
		case MIPI_DSI_FMT_RGB565:
			tmp |= PIX_FMT(PIX_FMT_RGB565);
			break;
		default:
			DRM_ERROR("DSI pixel format unsupported\n");
		}

		/* program DSI operation mode */
		if (intel_dsi->operation_mode == INTEL_DSI_VIDEO_MODE) {
			tmp &= OP_MODE_MASK;
			if (intel_dsi->video_mode_format ==
					VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE) {
				tmp |= OP_MODE(VIDEO_MODE_SYNC_PULSE);
			} else if (intel_dsi->video_mode_format ==
					VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS) {
				tmp |= OP_MODE(VIDEO_MODE_SYNC_EVENT);
			} else {
				DRM_ERROR("DSI Video Mode unsupported\n");
			}
		}

		I915_WRITE(DSI_TRANS_FUNC_CONF(dsi_trans), tmp);
	}

	/* enable port sync mode if dual link */
	if (intel_dsi->dual_link) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			tmp = I915_READ(TRANS_DDI_FUNC_CTL2(dsi_trans));
			tmp |= PORT_SYNC_MODE_ENABLE;
			I915_WRITE(TRANS_DDI_FUNC_CTL2(dsi_trans), tmp);
		}

		//TODO: configure DSS_CTL1
	}

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		/*select dala lane width */
		tmp = I915_READ(TRANS_DDI_FUNC_CTL(dsi_trans));
		tmp &= ~DDI_PORT_WIDTH_MASK;
		tmp |= DDI_PORT_WIDTH(intel_dsi->lane_count);
		tmp &= ~TRANS_DDI_EDP_INPUT_MASK;

		/* select input pipe */
		switch (pipe) {
		case PIPE_A:
			tmp |= TRANS_DDI_EDP_INPUT_A_ON;
			break;
		case PIPE_B:
			tmp |= TRANS_DDI_EDP_INPUT_B_ONOFF;
			break;
		case PIPE_C:
			tmp |= TRANS_DDI_EDP_INPUT_C_ONOFF;
			break;
		default:
			DRM_ERROR("invalid pipe select\n");
			break;
		}

		/* enable DDI buffer */
		tmp |= TRANS_DDI_FUNC_ENABLE;
		I915_WRITE(TRANS_DDI_FUNC_CTL(dsi_trans), tmp);
	}

	/* wait for link ready */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		if (wait_for_us((I915_READ(DSI_TRANS_FUNC_CONF(dsi_trans)) &
				LINK_READY), 500))
			DRM_ERROR("DSI link not ready\n");
	}
}

static void gen11_dsi_set_transocder_timings(struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	const struct drm_display_mode *adjusted_mode =
					&pipe_config->base.adjusted_mode;
	enum port port;
	u32 tmp;
	enum transcoder dsi_trans;
	/* horizontal timings */
	u16 htotal, hactive, hsync_start, hsync_end, hsync_size;
	u16 hfront_porch, hback_porch;
	/* vertical timings */
	u16 vtotal, vactive, vsync_start, vsync_end, vsync_shift;

	hactive = adjusted_mode->crtc_hdisplay;
	htotal = adjusted_mode->crtc_htotal;
	hsync_start = adjusted_mode->crtc_hsync_start;
	hsync_end = adjusted_mode->crtc_hsync_end;
	hsync_size  = hsync_end - hsync_start;
	hfront_porch = (adjusted_mode->crtc_hsync_start -
			adjusted_mode->crtc_hdisplay);
	hback_porch = (adjusted_mode->crtc_htotal -
		       adjusted_mode->crtc_hsync_end);
	vactive = adjusted_mode->crtc_vdisplay;
	vtotal = adjusted_mode->crtc_vtotal;
	vsync_start = adjusted_mode->crtc_vsync_start;
	vsync_end = adjusted_mode->crtc_vsync_end;
	vsync_shift = hsync_start - htotal/2;

	if (intel_dsi->dual_link) {
		hactive /= 2;
		if (intel_dsi->dual_link == DSI_DUAL_LINK_FRONT_BACK)
			hactive += intel_dsi->pixel_overlap;
		htotal /= 2;
	}

	/* minimum hactive as per bspec: 256 pixels */
	if (adjusted_mode->crtc_hdisplay < 256)
		DRM_ERROR("hactive is less then 256 pixels\n");

	/* if RGB666 format, then hactive must be multiple of 4 pixels */
	if ((intel_dsi->pixel_format == MIPI_DSI_FMT_RGB666) &&
	    ((hactive % 4) != 0))
		DRM_ERROR("hactive pixels are not multiple of 4\n");

	/* program TRANS_HTOTAL register */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		/* BSPEC: program required pixels minus 1 */
		tmp = (hactive - 1);
		tmp |= ((htotal - 1) << TRANS_TIMING_SHIFT);
		I915_WRITE(HTOTAL(dsi_trans), tmp);
	}

	/* TRANS_HSYNC register to be programmed only for video mode */
	if (intel_dsi->operation_mode == INTEL_DSI_VIDEO_MODE) {
		if (intel_dsi->video_mode_format ==
		    VIDEO_MODE_NON_BURST_WITH_SYNC_PULSE) {
			/* BSPEC: hsync size should be atleast 16 pixels */
			if (hsync_size < 16)
				DRM_ERROR("hsync size < 16 pixels\n");
		} else if (intel_dsi->video_mode_format ==
			   VIDEO_MODE_NON_BURST_WITH_SYNC_EVENTS) {
			if (hsync_size < hsync_start)
				DRM_ERROR("hsync size < hsync start\n");
		}

		if (hback_porch < 16)
			DRM_ERROR("hback porch < 16 pixels\n");

		if (intel_dsi->dual_link) {
			hsync_start /= 2;
			hsync_end /= 2;
		}

		// TODO: Check for hblank_start - hsync_start > 16

		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			/* BSPEC: program required pixels minus 1 */
			tmp = (hsync_start - 1);
			tmp |= ((hsync_end - 1) << TRANS_TIMING_SHIFT);
			I915_WRITE(HSYNC(dsi_trans), tmp);
		}
	}

	/* program TRANS_VTOTAL register */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		/*
		 * FIXME: Programing this by assuming progressive mode, since
		 * non-interlaced info from VBT is not saved inside
		 * struct drm_display_mode.
		 * For interlace mode: program required pixel minus 2
		 */
		tmp = ((vtotal - 1) << TRANS_TIMING_SHIFT);
		/* BSPEC: program required pixels minus 1 */
		tmp |= (vactive - 1);
		I915_WRITE(VTOTAL(dsi_trans), tmp);
	}

	if (vsync_end < vsync_start || vsync_end > vtotal)
		DRM_ERROR("Invalid vsync_end value\n");

	if (vsync_start < vactive)
		DRM_ERROR("vsync_start less than vactive\n");

	/* program TRANS_VSYNC register */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		/* BSPEC: program required pixels minus 1 */
		tmp = (vsync_start - 1);
		tmp |= ((vsync_end - 1) << TRANS_TIMING_SHIFT);
		I915_WRITE(VSYNC(dsi_trans), tmp);
	}

	/*
	 * FIXME: It has to be programmed only for interlaced
	 * modes. Put the check condition here once interlaced
	 * info available as described above.
	 * program TRANS_VSYNCSHIFT regsitser
	 */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = (vsync_shift << TRANS_TIMING_SHIFT);
		I915_WRITE(VSYNCSHIFT(dsi_trans), tmp);
	}
}

static void gen11_dsi_enable_transcoder(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	enum transcoder dsi_trans;
	u32 tmp;

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = I915_READ(PIPECONF(dsi_trans));
		tmp |= PIPECONF_ENABLE;
		I915_WRITE(PIPECONF(dsi_trans), tmp);

		/* wait for transcoder to be enabled */
		if (intel_wait_for_register(dev_priv, PIPECONF(dsi_trans),
					    I965_PIPECONF_ACTIVE,
					    I965_PIPECONF_ACTIVE, 10))
			DRM_ERROR("DSI transcoder not enabled\n");
	}
}

static void gen11_dsi_setup_timeouts(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	enum transcoder dsi_trans;
	u32 tmp, hs_tx_timeout, lp_rx_timeout, divisor, mul;

	/*
	 * escape clock count calculation:
	 * BYTE_CLK_COUNT = TIME_NS/(8 * UI)
	 * UI (nsec) = (10^6)/Bitrate
	 * TIME_NS = (BYTE_CLK_COUNT * 8 * 10^6)/ Bitrate
	 * ESCAPE_CLK_COUNT  = TIME_NS/ESC_CLK_NS
	 */
	divisor = intel_dsi->escape_clk_nsec *  intel_dsi->bitrate_khz * 10^3;
	mul = 8 * 10^6;
	hs_tx_timeout = DIV_ROUND_UP(intel_dsi->hs_tx_timeout * mul,
				     divisor);
	lp_rx_timeout = DIV_ROUND_UP(intel_dsi->lp_rx_timeout * mul,
				     divisor);

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);

		/* program hst_tx_timeout */
		tmp = I915_READ(DSI_HSTX_TO(dsi_trans));
		tmp &= ~HSTX_TIMEOUT_VALUE_MASK;
		tmp |= HSTX_TIMEOUT_VALUE(hs_tx_timeout);
		I915_WRITE(DSI_HSTX_TO(dsi_trans), tmp);

		/* program lp_rx_host timeout */
		tmp = I915_READ(DSI_LPRX_HOST_TO(dsi_trans));
		tmp &= ~LPRX_TIMEOUT_VALUE_MASK;
		tmp |= LPRX_TIMEOUT_VALUE(lp_rx_timeout);
		I915_WRITE(DSI_HSTX_TO(dsi_trans), tmp);
	}
}

static void gen11_dsi_enable_port_and_phy(struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config)
{
	/* step 4a: power up all lanes of the DDI used by DSI */
	gen11_dsi_power_up_lanes(encoder);

	/* step 4b: configure lane sequencing of the Combo-PHY transmitters */
	gen11_dsi_config_phy_lanes_sequence(encoder);

	/* step 4c: configure voltage swing and skew */
	gen11_dsi_voltage_swing_program_seq(encoder);

	/* step 4d: enable DDI buffer */
	gen11_dsi_enable_ddi_buffer(encoder);

	/* step 4e: setup D-PHY timings */
	gen11_dsi_setup_dphy_timings(encoder);

	/* step 4g: setup DSI protocol timeouts */
	gen11_dsi_setup_timeouts(encoder);

	/* Step (4h, 4i, 4j, 4k): Configure transcoder */
	gen11_dsi_configure_transcoder(encoder, pipe_config);
}

static void gen11_dsi_powerup_panel(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct mipi_dsi_device *dsi;
	enum port port;
	enum transcoder dsi_trans;
	u32 tmp;
	int ret;

	/* set maximum return packet size */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = I915_READ(DSI_CMD_RXCTL(dsi_trans));
		tmp &= RX_PLOAD_DW_MASK;
		/* multiply "Number Rx Payload DW" by 4 to get max value */
		tmp = tmp * 4;
		dsi = to_mipi_dsi_device(intel_dsi->dsi_hosts[port]->base.dev);
		ret = mipi_dsi_set_maximum_return_packet_size(dsi, tmp);
		if (ret < 0)
			DRM_ERROR("error setting max return pkt size%d\n", tmp);
	}

	/* power on */
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_POWER_ON);
	intel_dsi_msleep(intel_dsi, intel_dsi->panel_on_delay);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DEASSERT_RESET);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_INIT_OTP);

	/*send TURN_ON command to panel */
	if (intel_dsi->operation_mode == INTEL_DSI_VIDEO_MODE) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi = to_mipi_dsi_device(
					intel_dsi->dsi_hosts[port]->base.dev);
			ret = mipi_dsi_turn_on_peripheral(dsi);
			if (ret < 0)
				DRM_ERROR("error sending TURN_ON command\n");
		}
	}

	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DISPLAY_ON);

	/* ensure all panel commands dispatched before enabling transcoder */
	wait_for_cmds_dispatched_to_panel(encoder);
}

static void __attribute__((unused)) gen11_dsi_pre_enable(
				struct intel_encoder *encoder,
				const struct intel_crtc_state *pipe_config,
				const struct drm_connector_state *conn_state)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);

	/* step2: enable IO power */
	gen11_dsi_enable_io_power(encoder);

	/* step3: enable DSI PLL */
	gen11_dsi_program_esc_clk_div(encoder);

	/* step4: enable DSI port and DPHY */
	gen11_dsi_enable_port_and_phy(encoder, pipe_config);

	/* step5: program and powerup panel */
	gen11_dsi_powerup_panel(encoder);

	/* step6c: configure transcoder timings */
	gen11_dsi_set_transocder_timings(encoder, pipe_config);

	/* step6d: enable dsi transcoder */
	gen11_dsi_enable_transcoder(encoder);

	/* step7: enable backlight */
	intel_panel_enable_backlight(pipe_config, conn_state);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_BACKLIGHT_ON);
}

static void gen11_dsi_disable_transcoder(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	enum transcoder dsi_trans;
	u32 tmp;

	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);

		/* disable transcoder */
		tmp = I915_READ(PIPECONF(dsi_trans));
		tmp &= ~PIPECONF_ENABLE;
		I915_WRITE(PIPECONF(dsi_trans), tmp);

		/* wait for transcoder to be disabled */
		if (intel_wait_for_register(dev_priv, PIPECONF(dsi_trans),
					    I965_PIPECONF_ACTIVE,
					    I965_PIPECONF_ACTIVE, 50))
			DRM_ERROR("DSI trancoder not disabled\n");
	}
}
static void gen11_dsi_powerdown_panel(struct intel_encoder *encoder)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	struct mipi_dsi_device *dsi;
	enum port port;
	int ret;

	/* send SHUT_DOWN command to panel */
	if (intel_dsi->operation_mode == INTEL_DSI_VIDEO_MODE) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi = to_mipi_dsi_device(
					intel_dsi->dsi_hosts[port]->base.dev);
			ret = mipi_dsi_shutdown_peripheral(dsi);
			if (ret < 0)
				DRM_ERROR("error sending SHUT_DOWN command\n");
		}
	}

	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_DISPLAY_OFF);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_ASSERT_RESET);
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_POWER_OFF);

	/* ensure cmds dispatched to panel */
	wait_for_cmds_dispatched_to_panel(encoder);
}

static void gen11_dsi_deconfigure_trancoder(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	enum transcoder dsi_trans;
	u32 tmp;

	/* put dsi link in ULPS */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = I915_READ(DSI_LP_MSG(dsi_trans));
		tmp |= LINK_ENTER_ULPS;
		I915_WRITE(DSI_LP_MSG(dsi_trans), tmp);

		if (wait_for_us((I915_READ(DSI_LP_MSG(dsi_trans)) &
				LINK_IN_ULPS),
				10))
			DRM_ERROR("DSI link not in ULPS\n");
	}

	/* disable ddi function */
	for_each_dsi_port(port, intel_dsi->ports) {
		dsi_trans = dsi_port_to_transcoder(port);
		tmp = I915_READ(TRANS_DDI_FUNC_CTL(dsi_trans));
		tmp &= ~TRANS_DDI_FUNC_ENABLE;
		I915_WRITE(TRANS_DDI_FUNC_CTL(dsi_trans), tmp);
	}

	/* disable port sync mode if dual link */
	if (intel_dsi->dual_link) {
		for_each_dsi_port(port, intel_dsi->ports) {
			dsi_trans = dsi_port_to_transcoder(port);
			tmp = I915_READ(TRANS_DDI_FUNC_CTL2(dsi_trans));
			tmp &= ~PORT_SYNC_MODE_ENABLE;
			I915_WRITE(TRANS_DDI_FUNC_CTL2(dsi_trans), tmp);
		}
	}
}

static void gen11_dsi_disable_port(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	u32 tmp;
	enum port port;

	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(DDI_BUF_CTL(port));
		tmp &= ~DDI_BUF_CTL_ENABLE;
		I915_WRITE(DDI_BUF_CTL(port), tmp);

		if (wait_for_us((I915_READ(DDI_BUF_CTL(port)) &
				 DDI_BUF_IS_IDLE),
				 8))
			DRM_ERROR("DDI port:%c buffer not idle\n",
				  port_name(port));
	}
}

static void gen11_dsi_disable_io_power(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);
	enum port port;
	u32 tmp;

	/*
	 * TODO:Disable power request (step 4a,b) using
	 * intel_display_power_put(). Power domains for ICL
	 * port a, b are not defined.
	 */

	/* set mode to DDI */
	for_each_dsi_port(port, intel_dsi->ports) {
		tmp = I915_READ(ICL_DSI_IO_MODECTL(port));
		tmp &= ~COMBO_PHY_MODE_DSI;
		I915_WRITE(ICL_DSI_IO_MODECTL(port), tmp);
	}
}


static void __attribute__((unused)) gen11_dsi_disable(
			struct intel_encoder *encoder,
			const struct intel_crtc_state *old_crtc_state,
			const struct drm_connector_state *old_conn_state)
{
	struct intel_dsi *intel_dsi = enc_to_intel_dsi(&encoder->base);

	/* step1: turn off backlight */
	intel_dsi_vbt_exec_sequence(intel_dsi, MIPI_SEQ_BACKLIGHT_OFF);
	intel_panel_disable_backlight(old_conn_state);

	/* step2c,d: disable transcoder and wait */
	gen11_dsi_disable_transcoder(encoder);

	/* step2e,f: powerdown panel */
	gen11_dsi_powerdown_panel(encoder);

	/* step2g,h,i: deconfig trancoder */
	gen11_dsi_deconfigure_trancoder(encoder);

	/* step3: disable port */
	gen11_dsi_disable_port(encoder);

	/* step4: disable IO power */
	gen11_dsi_disable_io_power(encoder);
}
