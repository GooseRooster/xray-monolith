#pragma once

// OWA: XeGTAO Compute Shader Blender
// Replaces pixel shader XeGTAO with compute shaders for groupshared depth preload,
// UAV output (eliminates CopyResource), and future async compute overlap.

class CBlender_CS_XeGTAO : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: XeGTAO Compute"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);
};
