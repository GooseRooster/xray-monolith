#include "stdafx.h"
#pragma hdrstop

#include "blender_bloom_multiscale.h"

// OWA Multi-Scale Bloom: Kawase Downsample
// 5 shader elements for 5 downsample passes (D2->D4->D8->D16->D32)

CBlender_bloom_downsample::CBlender_bloom_downsample() { description.CLS = 0; }
CBlender_bloom_downsample::~CBlender_bloom_downsample() {}

void CBlender_bloom_downsample::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // D2: Full res -> 1/2 res (uses bloom_build output, applies threshold)
		C.r_Pass("stub_screen_space", "bloom_downsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_bloom", r2_RT_bloom1);  // Initial extraction from bloom_build
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 1: // D4: 1/2 -> 1/4 res
		C.r_Pass("stub_screen_space", "bloom_downsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_bloom", r2_RT_bloom_d2);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 2: // D8: 1/4 -> 1/8 res
		C.r_Pass("stub_screen_space", "bloom_downsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_bloom", r2_RT_bloom_d4);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 3: // D16: 1/8 -> 1/16 res
		C.r_Pass("stub_screen_space", "bloom_downsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_bloom", r2_RT_bloom_d8);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 4: // D32: 1/16 -> 1/32 res (wide haze)
		C.r_Pass("stub_screen_space", "bloom_downsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_bloom", r2_RT_bloom_d16);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
}

// OWA Multi-Scale Bloom: Tent Filter Upsample
// 4 shader elements for 4 upsample passes (D32->D16->D8->D4->D2)
// Each pass reads from current level + smaller level, writes to current level
// s_bloom = current level (being upsampled into), s_bloom_small = smaller level (accumulated from)

CBlender_bloom_upsample::CBlender_bloom_upsample() { description.CLS = 0; }
CBlender_bloom_upsample::~CBlender_bloom_upsample() {}

void CBlender_bloom_upsample::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // U16: output to D16, blend D16 content with D32
		C.r_Pass("stub_screen_space", "bloom_upsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_bloom", r2_RT_bloom_d16);      // Current level content
		C.r_dx10Texture("s_bloom_small", r2_RT_bloom_d32); // Smaller level (accumulated)
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 1: // U8: output to D8, blend D8 content with D16 (which now has D32 accumulated)
		C.r_Pass("stub_screen_space", "bloom_upsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_bloom", r2_RT_bloom_d8);
		C.r_dx10Texture("s_bloom_small", r2_RT_bloom_d16);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 2: // U4: output to D4, blend D4 content with D8
		C.r_Pass("stub_screen_space", "bloom_upsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_bloom", r2_RT_bloom_d4);
		C.r_dx10Texture("s_bloom_small", r2_RT_bloom_d8);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 3: // U2: output to D2, blend D2 content with D4 (final output)
		C.r_Pass("stub_screen_space", "bloom_upsample", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_bloom", r2_RT_bloom_d2);
		C.r_dx10Texture("s_bloom_small", r2_RT_bloom_d4);
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
}
