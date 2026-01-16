#pragma once

// refs
struct FSlideWindowItem;

#include "FBasicVisual.h"

//-----------------------------------------------------------------------------
// FTreeVisual - Base class for tree visuals with GPU instancing support
//
// Trees use a batched rendering approach for better performance:
// 1. Collection phase: Trees are grouped by CRC (identical geometry)
// 2. Render phase: Batched trees are rendered via RenderInstanced()
//
// The old Render() method is kept for backward compatibility but the
// primary rendering path uses RenderInstanced() for batched trees.
//-----------------------------------------------------------------------------
class FTreeVisual : public dxRender_Visual, public IRender_Mesh
{
public:
	virtual void Render(float LOD) override;
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
	virtual void Copy(dxRender_Visual* pFrom);
	virtual void Release();

	FTreeVisual(void);
	virtual ~FTreeVisual(void);

protected:
	// Helper for instanced rendering - maps instance data and issues draw call
	void DoRenderInstanced(const xr_vector<FloraVertData*>& data, u32 countV, u32 startI, u32 PC);
};

class FTreeVisual_ST : public FTreeVisual
{
	typedef FTreeVisual inherited;
public:
	FTreeVisual_ST(void);
	virtual ~FTreeVisual_ST(void);

	virtual void Render(float LOD) override;
	virtual void RenderInstanced(const xr_vector<FloraVertData*>& data) override;
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
	virtual void Copy(dxRender_Visual* pFrom);
	virtual void Release();
private:
	FTreeVisual_ST(const FTreeVisual_ST& other);
	void operator=(const FTreeVisual_ST& other);
};

class FTreeVisual_PM : public FTreeVisual
{
	typedef FTreeVisual inherited;
public:
	// Public for access during tree batching (LOD selection in r_dsgraph_insert_static)
	FSlideWindowItem* pSWI{};
	u32 last_lod{};

public:
	FTreeVisual_PM(void);
	virtual ~FTreeVisual_PM(void);

	virtual void Render(float LOD) override;
	virtual void RenderInstanced(const xr_vector<FloraVertData*>& data) override;
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
	virtual void Copy(dxRender_Visual* pFrom);
	virtual void Release();
private:
	FTreeVisual_PM(const FTreeVisual_PM& other);
	void operator=(const FTreeVisual_PM& other);
};

const int FTreeVisual_tile = 16;
const int FTreeVisual_quant = 32768 / FTreeVisual_tile;
