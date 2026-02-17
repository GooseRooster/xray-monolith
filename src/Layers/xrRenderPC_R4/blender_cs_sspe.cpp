#include "stdafx.h"
#pragma hdrstop

#include "blender_cs_sspe.h"

// OWA: Screen-Space Probe Enhancement (SSPE) Compute Shader Blender
// Element 0: 8-sample structured bounce + temporal accumulation with camera reprojection
//
// Reads G-buffer position (depth + normals for geometry tests), previous frame's
// composited scene (post-TAA lit surfaces), motion vectors, and previous SSPE for temporal.
// Outputs via UAV: rt_sspe (RGB = bounce color, A = confidence)

void CBlender_CS_SSPE::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // Main SSPE pass (compute shader)
		C.r_ComputePass("sspe_main");

		// G-buffer position (depth for rejection, normals for geometric weights)
		C.r_dx10Texture("s_position", r2_RT_P);

		// Previous frame's composited scene — stable copy made after combine_1 finishes.
		// rt_Generic_0 is volatile (cleared at start of combine, overwritten by water/forward/AA),
		// so we read from rt_sspe_scene which persists across frames via CopyResource.
		C.r_dx10Texture("s_prev_scene", r2_RT_sspe_scene);

		// Temporal inputs
		C.r_dx10Texture("s_sspe_prev", r2_RT_sspe_prev);
		C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);

		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
}
