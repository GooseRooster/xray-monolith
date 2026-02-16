#pragma once

// OWA: Probe Volume Sparse Update Compute Shader Blender
// Element 0: Scatter dirty voxels from structured buffer into volume UAVs
class CBlender_CS_ProbeVolume : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: Probe Volume Update"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }
	virtual void Compile(CBlender_Compile& C);
};
