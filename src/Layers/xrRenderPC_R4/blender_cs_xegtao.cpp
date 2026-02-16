#include "stdafx.h"
#pragma hdrstop

#include "blender_cs_xegtao.h"

// OWA: XeGTAO Compute Shader Blender
// Element 0: Main GTAO pass (compute) - reads G-buffer, outputs to UAV
// Element 1: Edge-aware denoise pass (compute) - reads main output, outputs to UAV

void CBlender_CS_XeGTAO::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // Main XeGTAO pass (compute shader)
		C.r_ComputePass("xegtao_main");

		// G-buffer input: depth in .z, packed normals in .xy (decoded via gbuf_unpack_normal)
		C.r_dx10Texture("s_position", r2_RT_P);

		C.r_dx10Sampler("smp_nofilter");
		C.r_End();
		break;

	case 1: // Denoise pass (compute shader)
		C.r_ComputePass("xegtao_denoise");

		// Read main pass output from rt_gtao_temp
		// Phase function writes main output to rt_gtao_temp when denoise is enabled,
		// then denoise reads it here and writes denoised result to rt_gtao via UAV.
		C.r_dx10Texture("s_gtao", r2_RT_gtao_temp);
		C.r_dx10Texture("s_gtao_edges", r2_RT_gtao_edges);

		C.r_dx10Sampler("smp_nofilter");
		C.r_End();
		break;
	}
}
