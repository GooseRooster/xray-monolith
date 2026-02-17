#pragma once

// OWA: Screen-Space Probe Enhancement (SSPE) Compute Shader Blender
// Structured 8-sample screen-space color bounce with temporal EMA.
// Additive on top of probe GI — when screen-space data is unavailable,
// probes provide the stable base layer (zero visual pop).

class CBlender_CS_SSPE : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: SSPE Compute"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);
};
