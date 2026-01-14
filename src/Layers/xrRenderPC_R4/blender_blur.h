#pragma once


class CBlender_blur : public IBlender
{
public:
	virtual LPCSTR getComment() { return "Blur generation"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_blur();
	virtual ~CBlender_blur();
};

// SSS
class CBlender_ssfx_volumetric_blur : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ssfx_volumetric_blur"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ssfx_volumetric_blur();
	virtual ~CBlender_ssfx_volumetric_blur();
};

class CBlender_ssfx_sss : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ssfx_sss"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ssfx_sss();
	virtual ~CBlender_ssfx_sss();
};

class CBlender_ssfx_sss_ext : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ssfx_sss_ext"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ssfx_sss_ext();
	virtual ~CBlender_ssfx_sss_ext();
};

class CBlender_ssfx_water_blur : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ssfx_water"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ssfx_water_blur();
	virtual ~CBlender_ssfx_water_blur();
};

class CBlender_ssfx_fog_scattering : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ssfx_fog_scattering"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ssfx_fog_scattering();
	virtual ~CBlender_ssfx_fog_scattering();
};

// Indirect Lighting (separated from AO)
class CBlender_ssfx_il : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ssfx_il"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ssfx_il();
	virtual ~CBlender_ssfx_il();
};

// OWA: Perceptual Lighting cascaded blur
class CBlender_blur_pl : public IBlender
{
public:
	virtual LPCSTR getComment() { return "Perceptual Lighting blur"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_blur_pl();
	virtual ~CBlender_blur_pl();
};

// OWA: Perceptual Lighting final composite (post-PP)
class CBlender_perceptual_lighting : public IBlender
{
public:
	virtual LPCSTR getComment() { return "Perceptual Lighting composite"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_perceptual_lighting();
	virtual ~CBlender_perceptual_lighting();
};