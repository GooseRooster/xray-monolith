#pragma once

// OWA Multi-Scale Bloom Blenders
// Kawase downsample and tent filter upsample for hierarchical bloom

class CBlender_bloom_downsample : public IBlender
{
public:
	virtual LPCSTR getComment() { return "OWA: bloom downsample"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_bloom_downsample();
	virtual ~CBlender_bloom_downsample();
};

class CBlender_bloom_upsample : public IBlender
{
public:
	virtual LPCSTR getComment() { return "OWA: bloom upsample"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_bloom_upsample();
	virtual ~CBlender_bloom_upsample();
};
