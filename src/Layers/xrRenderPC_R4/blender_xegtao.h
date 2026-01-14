#pragma once

// OWA: XeGTAO - Intel's Ground Truth Ambient Occlusion
// Based on Intel XeGTAO from Luma Framework
// Two-pass approach: main AO calculation + edge-aware denoise

class CBlender_XeGTAO : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: XeGTAO"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_XeGTAO();
	virtual ~CBlender_XeGTAO();
};

class CBlender_XeGTAO_MSAA : public IBlender
{
public:
	virtual LPCSTR getComment() { return "INTERNAL: XeGTAO MSAA"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_XeGTAO_MSAA();
	virtual ~CBlender_XeGTAO_MSAA();

	virtual void SetDefine(LPCSTR Name, LPCSTR Definition)
	{
		this->Name = Name;
		this->Definition = Definition;
	}

	LPCSTR Name;
	LPCSTR Definition;
};
