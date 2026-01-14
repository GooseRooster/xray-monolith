#include "stdafx.h"
#pragma hdrstop

#include "blender_xegtao.h"

// OWA: XeGTAO - Intel's Ground Truth Ambient Occlusion
// Element 0: Main XeGTAO pass - outputs to rt_gtao (bent normal + obscurance) and rt_gtao_edges
// Element 1: Edge-aware denoise pass - reads rt_gtao and rt_gtao_edges, outputs final AO

CBlender_XeGTAO::CBlender_XeGTAO() { description.CLS = 0; }

CBlender_XeGTAO::~CBlender_XeGTAO()
{
}

void CBlender_XeGTAO::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // XeGTAO main pass - outputs AO + edges (MRT)
		C.r_Pass("combine_1", "xegtao", FALSE, FALSE, FALSE);
		// OWA: No stencil testing - GTAO is a fullscreen pass that should process all pixels
		// Sky pixels will have infinite depth and produce no occlusion naturally
		C.r_Stencil(FALSE);
		C.r_CullMode(D3DCULL_NONE);

		// GBuffer textures for depth and normal reconstruction
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_normal", r2_RT_N);

		// Jitter textures for spatial noise
		jitter(C);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;

	case 1: // XeGTAO denoise pass - edge-aware blur
		C.r_Pass("combine_1", "xegtao_denoise", FALSE, FALSE, FALSE);
		// OWA: No stencil testing - fullscreen pass
		C.r_Stencil(FALSE);
		C.r_CullMode(D3DCULL_NONE);

		// Input from main pass - read from temp RT to avoid read/write hazard
		// (phase_xegtao copies rt_gtao to rt_gtao_temp before this pass)
		C.r_dx10Texture("s_gtao", r2_RT_gtao_temp);
		C.r_dx10Texture("s_gtao_edges", r2_RT_gtao_edges);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
}

// MSAA variant
CBlender_XeGTAO_MSAA::CBlender_XeGTAO_MSAA() { description.CLS = 0; }

CBlender_XeGTAO_MSAA::~CBlender_XeGTAO_MSAA()
{
}

void CBlender_XeGTAO_MSAA::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	if (Name)
		::Render->m_MSAASample = atoi(Definition);
	else
		::Render->m_MSAASample = -1;

	switch (C.iElement)
	{
	case 0: // XeGTAO main pass - MSAA variant
		C.r_Pass("combine_1", "xegtao_msaa", FALSE, FALSE, FALSE);
		// OWA: No stencil testing - fullscreen pass
		C.r_Stencil(FALSE);
		C.r_CullMode(D3DCULL_NONE);

		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_normal", r2_RT_N);

		jitter(C);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;

	case 1: // XeGTAO denoise pass - MSAA variant
		C.r_Pass("combine_1", "xegtao_denoise_msaa", FALSE, FALSE, FALSE);
		// OWA: No stencil testing - fullscreen pass
		C.r_Stencil(FALSE);
		C.r_CullMode(D3DCULL_NONE);

		// Input from main pass - read from temp RT to avoid read/write hazard
		C.r_dx10Texture("s_gtao", r2_RT_gtao_temp);
		C.r_dx10Texture("s_gtao_edges", r2_RT_gtao_edges);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
	::Render->m_MSAASample = -1;
}
