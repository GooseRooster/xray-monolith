#include "stdafx.h"
#pragma hdrstop

#include "blender_cs_probe_volume.h"

// OWA: Probe Volume Sparse Update Compute Shader Blender
// Element 0: Scatter dirty voxels from structured buffer into volume UAVs
// No texture bindings — structured buffer SRV and volume UAVs are bound
// manually in phase_probe_volume_update() since they're dynamic resources
// not registered in the engine's texture system.

void CBlender_CS_ProbeVolume::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: // Sparse volume update pass (compute shader)
		C.r_ComputePass("probe_volume_update");
		C.r_End();
		break;
	}
}
