#include "stdafx.h"

#include "blender_blur.h"

CBlender_blur::CBlender_blur() { description.CLS = 0; }

CBlender_blur::~CBlender_blur()
{
}

void CBlender_blur::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:	//Fullres Horizontal
		C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 1:	//Fullres Vertical
		C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_blur_h_2);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 2: //Halfres Horizontal
		C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 3: //Halfres Vertical
		C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_blur_h_4);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 4: //Quarterres Horizontal
		C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 5: //Quarterres Vertical
		C.r_Pass("stub_screen_space", "pp_blur", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_blur_h_8);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_lut_atlas", "shaders\\lut_atlas");

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
}


CBlender_ssfx_volumetric_blur::CBlender_ssfx_volumetric_blur() { description.CLS = 0; }

CBlender_ssfx_volumetric_blur::~CBlender_ssfx_volumetric_blur()
{
}

void CBlender_ssfx_volumetric_blur::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:	// Blur Phase 1
		C.r_Pass("stub_screen_space", "ssfx_volumetric_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("vol_buffer", r2_RT_ssfx_volumetric);

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;

	case 1:	// Blur Phase 2
		C.r_Pass("stub_screen_space", "ssfx_volumetric_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("vol_buffer", r2_RT_ssfx_volumetric_tmp);

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;

	case 2:	// Blur Phase 1
		C.r_Pass("stub_screen_space", "ssfx_volumetric_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("vol_buffer", r2_RT_generic2);

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;

	case 3:	// Blur Phase 2
		C.r_Pass("stub_screen_space", "ssfx_volumetric_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("vol_buffer", r2_RT_ssfx_accum);

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;

	case 5:	// Combine
		C.r_Pass("stub_screen_space", "ssfx_volumetric_combine", FALSE, FALSE, FALSE);

		C.r_dx10Texture("vol_buffer", r2_RT_generic2);
		C.r_dx10Texture("vol_point", r2_RT_ssfx_volumetric);

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;
	}
}


CBlender_ssfx_sss::CBlender_ssfx_sss() { description.CLS = 0; }

CBlender_ssfx_sss::~CBlender_ssfx_sss()
{
}

void CBlender_ssfx_sss::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:	// SSS
		C.r_Pass("stub_screen_space", "ssfx_sss", FALSE, FALSE, FALSE);

		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
		C.r_dx10Texture("sss_image", r2_RT_ssfx_sss);

		C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);

		C.r_dx10Texture("jitter0", JITTER(0));

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_dx10Sampler("smp_jitter");

		C.r_End();
		break;

	case 1:	// SSS Blur Occ Mask - Phase 1
		C.r_Pass("stub_screen_space", "ssfx_sss_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("sss_image", r2_RT_ssfx);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_dx10Sampler("smp_jitter");

		C.r_End();
		break;

	case 2:	// SSS Blur Occ Mask - Phase 2
		C.r_Pass("stub_screen_space", "ssfx_sss_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("sss_image", r2_RT_ssfx_temp);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_dx10Sampler("smp_jitter");

		C.r_End();
		break;

	}
}


CBlender_ssfx_sss_ext::CBlender_ssfx_sss_ext() { description.CLS = 0; }

CBlender_ssfx_sss_ext::~CBlender_ssfx_sss_ext()
{
}

void CBlender_ssfx_sss_ext::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // SSS Ext
		C.r_Pass("stub_screen_space", "ssfx_sss_ext", FALSE, FALSE, FALSE);

		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
		C.r_dx10Texture("sss_image", r2_RT_ssfx_sss_ext);

		C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_dx10Sampler("smp_jitter");

		C.r_End();
		break;

	case 1: // SSS Ext
		C.r_Pass("stub_screen_space", "ssfx_sss_ext", FALSE, FALSE, FALSE);

		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
		C.r_dx10Texture("sss_image", r2_RT_ssfx_sss_ext2);

		C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_dx10Sampler("smp_jitter");

		C.r_End();
		break;

	case 2: // SSS Ext Combine

		C.r_Pass("stub_screen_space", "ssfx_sss_ext_combine", FALSE, FALSE, FALSE);

		C.r_dx10Texture("sss_image_1", r2_RT_ssfx_sss_ext);
		C.r_dx10Texture("sss_image_2", r2_RT_ssfx_sss_ext2);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");

		C.r_End();
		break;
	}
}

CBlender_ssfx_water_blur::CBlender_ssfx_water_blur() { description.CLS = 0; }

CBlender_ssfx_water_blur::~CBlender_ssfx_water_blur()
{
}

void CBlender_ssfx_water_blur::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:	// Blur Phase 1
		C.r_Pass("stub_screen_space", "ssfx_water_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("water_buffer", r2_RT_ssfx_temp);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;

	case 1:	// Blur Phase 2
		C.r_Pass("stub_screen_space", "ssfx_water_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("water_buffer", r2_RT_ssfx_temp2);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;

	case 2:	// No Blur
		C.r_Pass("stub_screen_space", "ssfx_water_noblur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("water_buffer", r2_RT_ssfx_temp2);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;

	case 5:	// Water Waves
		C.r_Pass("stub_screen_space", "ssfx_water_waves", FALSE, FALSE, FALSE);

		C.r_dx10Texture("water_waves", "fx\\water_height");

		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;
	}
}

CBlender_ssfx_fog_scattering::CBlender_ssfx_fog_scattering() { description.CLS = 0; }

CBlender_ssfx_fog_scattering::~CBlender_ssfx_fog_scattering()
{
}

void CBlender_ssfx_fog_scattering::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:
		C.r_Pass("stub_screen_space", "ssfx_fog_scattering", FALSE, FALSE, FALSE);

		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
		//C.r_dx10Texture("ssfx_motionvectors", r2_RT_ssfx_motion_vectors);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;


	case 2:
		C.r_Pass("stub_screen_space", "ssfx_fog_scattering_blur", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_generic0);

		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 3:
		C.r_Pass("stub_screen_space", "ssfx_fog_scattering_blur", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_blur_4);

		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
}

// Indirect Lighting (separated from AO)
CBlender_ssfx_il::CBlender_ssfx_il() { description.CLS = 0; }

CBlender_ssfx_il::~CBlender_ssfx_il()
{
}

void CBlender_ssfx_il::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0:	// IL
		C.r_Pass("stub_screen_space", "ssfx_il", FALSE, FALSE, FALSE);

		C.r_dx10Texture("s_accumulator", r2_RT_accum);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
		// OWA XeGTAO: Bent normal for improved IL directional accuracy
		C.r_dx10Texture("s_gtao", r2_RT_gtao);
		// OWA: Probe-based ambient lighting texture
		C.r_dx10Texture("s_probe_grid", r2_T_probe_grid);

		C.r_dx10Texture("ssfx_ao", r2_RT_ssfx_il);

		C.r_dx10Texture("s_prev_pos", r2_RT_ssfx_prevPos);

		C.r_dx10Texture("jitter0", JITTER(0));

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_dx10Sampler("smp_jitter");

		C.r_End();

		break;

	case 1:	// Blur Phase 1
		C.r_Pass("stub_screen_space", "ssfx_il_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("ao_image", r2_RT_ssfx_temp2);
		C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;

	case 2:	// Blur Phase 2
		C.r_Pass("stub_screen_space", "ssfx_il_blur", FALSE, FALSE, FALSE);

		C.r_dx10Texture("ao_image", r2_RT_ssfx_temp3);
		C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;
	}
}

// OWA: Perceptual Lighting - FGFX LSPOIrr implementation
// Progressive downsampling and cascaded blur
// NOTE: Limited to 6 elements (0-5) for engine compatibility
CBlender_blur_pl::CBlender_blur_pl() { description.CLS = 0; }

CBlender_blur_pl::~CBlender_blur_pl()
{
}

void CBlender_blur_pl::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	// Progressive downsampling chain (energy-conservative)
	case 0: // Downsample: pl_source (full) -> pl_half (1/2)
		C.r_Pass("stub_screen_space", "pp_pl_downsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_pl_source);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 1: // Downsample: pl_half (1/2) -> pl_quad (1/4)
		C.r_Pass("stub_screen_space", "pp_pl_downsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_pl_half);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 2: // Downsample: pl_quad (1/4) -> pl_octo (1/8)
		C.r_Pass("stub_screen_space", "pp_pl_downsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_pl_quad);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 3: // Downsample: pl_octo (1/8) -> pl_hexa (1/16)
		C.r_Pass("stub_screen_space", "pp_pl_downsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_pl_octo);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;

	// Cascaded blur ping-pong (only 2 elements needed)
	case 4: // Blur: read from vblur, write to hblur (H passes)
		C.r_Pass("stub_screen_space", "pp_blur_pl", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_pl_vblur);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 5: // Blur: read from hblur, write to vblur (V passes)
		C.r_Pass("stub_screen_space", "pp_blur_pl", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_pl_hblur);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
}

// OWA: Perceptual Lighting final composite (post-PP)
CBlender_perceptual_lighting::CBlender_perceptual_lighting() { description.CLS = 0; }

CBlender_perceptual_lighting::~CBlender_perceptual_lighting()
{
}

void CBlender_perceptual_lighting::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // Final composite - apply PL effect to backbuffer
		C.r_Pass("stub_screen_space", "pp_perceptual_lighting", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_image", r2_RT_pl_source);    // Post-PP image (full res)
		C.r_dx10Texture("s_blur_pl", r2_RT_pl_vblur);   // Long blur (cascaded, 1/16 res)
		C.r_dx10Texture("s_blur_short", r2_RT_pl_short); // Short blur (for recovery, 1/16 res)
		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_End();
		break;
	}
}
