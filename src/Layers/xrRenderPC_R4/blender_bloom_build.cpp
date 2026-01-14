#include "stdafx.h"
#pragma hdrstop

#include "Blender_bloom_build.h"

CBlender_bloom_build::CBlender_bloom_build() { description.CLS = 0; }

CBlender_bloom_build::~CBlender_bloom_build()
{
}

void CBlender_bloom_build::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // transfer into bloom-target (extraction pass for luminance + Kawase pyramid input)
		C.r_Pass("stub_notransform_build", "bloom_build", FALSE, FALSE, FALSE, FALSE, D3DBLEND_SRCALPHA,
		         D3DBLEND_INVSRCALPHA);
		C.r_dx10Texture("s_image", r2_RT_generic1);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	// OWA: Cases 1-4 (old gaussian filter passes) removed - replaced by multi-scale Kawase bloom
	}
}

CBlender_bloom_build_msaa::CBlender_bloom_build_msaa() { description.CLS = 0; }

CBlender_bloom_build_msaa::~CBlender_bloom_build_msaa()
{
}

void CBlender_bloom_build_msaa::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // transfer into bloom-target (extraction pass for luminance + Kawase pyramid input)
		C.r_Pass("stub_notransform_build", "bloom_build", FALSE, FALSE, FALSE, FALSE, D3DBLEND_SRCALPHA,
		         D3DBLEND_INVSRCALPHA);
		C.r_dx10Texture("s_image", r2_RT_generic1);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	// OWA: Cases 1-4 (old gaussian filter passes) removed - replaced by multi-scale Kawase bloom
	}
}

CBlender_postprocess_msaa::CBlender_postprocess_msaa() { description.CLS = 0; }

CBlender_postprocess_msaa::~CBlender_postprocess_msaa()
{
}

void CBlender_postprocess_msaa::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // transfer into bloom-target
		C.r_Pass("stub_notransform_postpr", "postprocess", FALSE, FALSE, FALSE, FALSE, D3DBLEND_SRCALPHA,
		         D3DBLEND_INVSRCALPHA);
		C.r_dx10Texture("s_base0", r2_RT_generic);
		C.r_dx10Texture("s_base1", r2_RT_generic);
		C.r_dx10Texture("s_noise", "fx\\fx_noise2");

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;

	case 4: // use color map
		C.r_Pass("stub_notransform_postpr", "postprocess_CM", FALSE, FALSE, FALSE, FALSE, D3DBLEND_SRCALPHA,
		         D3DBLEND_INVSRCALPHA);
		C.r_dx10Texture("s_base0", r2_RT_generic);
		C.r_dx10Texture("s_base1", r2_RT_generic);
		C.r_dx10Texture("s_noise", "fx\\fx_noise2");
		C.r_dx10Texture("s_grad0", "$user$cmap0");
		C.r_dx10Texture("s_grad1", "$user$cmap1");

		C.r_dx10Sampler("smp_rtlinear");
		C.r_dx10Sampler("smp_linear");
		C.r_End();
		break;
	}
}

