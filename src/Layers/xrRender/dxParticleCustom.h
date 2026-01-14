//---------------------------------------------------------------------------
#ifndef ParticleCustomH
#define ParticleCustomH

#include "../../Include/xrRender/ParticleCustom.h"
#include "FBasicVisual.h"

//---------------------------------------------------------------------------
class dxParticleCustom : public dxRender_Visual, public IParticleCustom
{
public:
	// geometry-format
	ref_geom geom;
	// OWA: HDR geometry with float4 color (for HDR10 mode)
	ref_geom geom_hdr;
public:
	virtual ~dxParticleCustom() { ; }

	virtual IParticleCustom* dcast_ParticleCustom() { return this; }
};

//---------------------------------------------------------------------------
#endif //ParticleCustomH
