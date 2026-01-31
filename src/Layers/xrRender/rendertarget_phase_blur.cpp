#include "stdafx.h"


IC bool SortLights(light* i, light* j)
{
	return (i->distance < j->distance&& i->sss_priority < j->sss_priority);
}

void CRenderTarget::phase_blur()
{
	//Get common data
	u32 Offset = 0;
	float d_Z = EPS_S;
	float d_W = 1.0f;
	u32 C = color_rgba(0, 0, 0, 255);

	//Full resolution
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
#if defined(USE_DX10) || defined(USE_DX11)
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);
#else
	p0.set(0.5f / w, 0.5f / h);
	p1.set((w + 0.5f) / w, (h + 0.5f) / h);
#endif

	///////////////////////////////////////////////////////////////////////////////////
	////Horizontal blur
	///////////////////////////////////////////////////////////////////////////////////
	w = float(Device.dwWidth) * 0.5f;
	h = float(Device.dwHeight) * 0.5f;

#if defined(USE_DX10) || defined(USE_DX11)
	u_setrt(rt_blur_h_2, 0, 0, 0);
#else
	u_setrt(rt_blur_h_2, 0, 0, rt_blur_2_zb);
#endif
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[0]);
	RCache.set_c("blur_params", 1.0, 0.0, w, h);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	///////////////////////////////////////////////////////////////////////////////////
	////Final blur
	///////////////////////////////////////////////////////////////////////////////////
#if defined(USE_DX10) || defined(USE_DX11)
	u_setrt(rt_blur_2, 0, 0, 0);
#else
	u_setrt(rt_blur_2, 0, 0, rt_blur_2_zb);
#endif
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[1]);
	RCache.set_c("blur_params", 0.0, 1.0, w, h);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	///////////////////////////////////////////////////////////////////////////////////
	////Horizontal blur / Half res
	///////////////////////////////////////////////////////////////////////////////////
	w = float(Device.dwWidth) * 0.25f;
	h = float(Device.dwHeight) * 0.25f;

#if defined(USE_DX10) || defined(USE_DX11)
	u_setrt(rt_blur_h_4, 0, 0, 0);
#else
	u_setrt(rt_blur_h_4, 0, 0, rt_blur_4_zb);
#endif
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[2]);
	RCache.set_c("blur_params", 1.0, 0.0, w, h);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	///////////////////////////////////////////////////////////////////////////////////
	////Final blur
	///////////////////////////////////////////////////////////////////////////////////
#if defined(USE_DX10) || defined(USE_DX11)
	u_setrt(rt_blur_4, 0, 0, 0);
#else
	u_setrt(rt_blur_4, 0, 0, rt_blur_4_zb);
#endif
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[3]);
	RCache.set_c("blur_params", 0.0, 1.0, w, h);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	///////////////////////////////////////////////////////////////////////////////////
	////Horizontal blur
	///////////////////////////////////////////////////////////////////////////////////
	w = float(Device.dwWidth) * 0.125f;
	h = float(Device.dwHeight) * 0.125f;

#if defined(USE_DX10) || defined(USE_DX11)
	u_setrt(rt_blur_h_8, 0, 0, 0);
#else
	u_setrt(rt_blur_h_8, 0, 0, rt_blur_8_zb);
#endif
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[4]);
	RCache.set_c("blur_params", 1.0, 0.0, w, h);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	///////////////////////////////////////////////////////////////////////////////////
	////Final blur
	///////////////////////////////////////////////////////////////////////////////////
#if defined(USE_DX10) || defined(USE_DX11)
	u_setrt(rt_blur_8, 0, 0, 0);
#else
	u_setrt(rt_blur_8, 0, 0, rt_blur_8_zb);
#endif
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_blur->E[5]);
	RCache.set_c("blur_params", 0.0, 1.0, w, h);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	///////////////////////////////////////////////////////////////////////////////////
};

#if defined(USE_DX11)
/**
 * OWA: Perceptual Lighting - FGFX LSPOIrr Implementation (R4/DX11 only)
 *
 * This implements the Fast Cascaded Separable Blur technique from FGFX::LSPOIrr
 * by Alex Tuduran. Key aspects that match the original:
 *
 * 1. Progressive downsampling (1/2 -> 1/4 -> 1/8 -> 1/16) - energy conservative
 * 2. Equal weight 3-tap blur (1:1:1) with 1.5x step multiplier
 * 3. Multiple cascade passes for smoothness:
 *    - Cascade 0 (offset 1): 4 times
 *    - Cascade 1 (offset 3): 1 time
 *    - Cascade 2 (offset 9): 1 time, then capture short blur, then 1 more time
 *    - Cascade 0 (offset 1): 1 time (ultra-smooth)
 * 4. Short blur captured at the right moment for recovery pass
 *
 * NOTE: phase_blur_pl() is no longer used - all PL processing happens in
 * phase_perceptual_lighting() which runs after phase_pp().
 */
void CRenderTarget::phase_blur_pl()
{
	// This function is deprecated - all PL processing now happens in phase_perceptual_lighting()
	// Kept for backward compatibility but does nothing
}

/**
 * Helper: Run a single H+V blur pass at the given offset
 * Uses elements 4 (read vblur) and 5 (read hblur) from s_blur_pl
 */
static void PL_BlurPass(CRenderTarget* RT, float offset, float radius, float w, float h,
                        ref_rt& rt_hblur, ref_rt& rt_vblur, ref_shader& s_blur_pl,
                        ref_geom& g_combine)
{
	u32 Offset = 0;
	float d_Z = EPS_S;
	float d_W = 1.0f;
	u32 C = color_rgba(0, 0, 0, 255);
	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);
	FVF::TL* pv;

	// Horizontal pass (read from vblur via element 4, write to hblur)
	RT->u_setrt(rt_hblur, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_blur_pl->E[4]); // Element 4: read from vblur
	RCache.set_c("blur_pl_params", 1.0f, 0.0f, w, h);  // Horizontal
	RCache.set_c("blur_pl_params2", offset, radius, 0.0f, 0.0f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	// Vertical pass (read from hblur via element 5, write to vblur)
	RT->u_setrt(rt_vblur, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_blur_pl->E[5]); // Element 5: read from hblur
	RCache.set_c("blur_pl_params", 0.0f, 1.0f, w, h);  // Vertical
	RCache.set_c("blur_pl_params2", offset, radius, 0.0f, 0.0f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

/**
 * OWA: Perceptual Lighting Final Composite (Post-PP) - R4/DX11 version
 *
 * This phase runs AFTER phase_pp() to apply perceptual lighting to the final image.
 * Matches the original FGFX::LSPOIrr ReShade implementation.
 *
 * Steps:
 * 1. Progressive downsample rt_pl_source to 1/16 res (4 stages)
 * 2. Run cascaded blur matching FGFX pattern
 * 3. Capture short blur at the right moment
 * 4. Apply PL composite shader back to HW.pBaseRT
 */
void CRenderTarget::phase_perceptual_lighting()
{
	// Get common data
	u32 Offset = 0;
	float d_Z = EPS_S;
	float d_W = 1.0f;
	u32 C = color_rgba(0, 0, 0, 255);

	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);
	float w2 = w * 0.5f;
	float h2 = h * 0.5f;
	float w4 = w * 0.25f;
	float h4 = h * 0.25f;
	float w8 = w * 0.125f;
	float h8 = h * 0.125f;
	float w16 = w * 0.0625f;
	float h16 = h * 0.0625f;

	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);

	FVF::TL* pv;

	// Get radius parameter from console (ps_r3_gi_pl_params2.x, default 0.65)
	float radius = ps_r3_gi_pl_params2.x;

	// rt_pl_source already contains the post-PP image (phase_pp wrote to it)

	//////////////////////////////////////////////////////////////////////////////
	// Step 1: Progressive Downsampling (Energy-Conservative)
	// This is critical for proper FGFX behavior - we need to account for ALL pixels
	//////////////////////////////////////////////////////////////////////////////

	// 1a: pl_source (full) -> pl_half (1/2)
	u_setrt(rt_pl_half, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h2, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w2, h2, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w2, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_blur_pl->E[0]); // Downsample from pl_source
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	// 1b: pl_half (1/2) -> pl_quad (1/4)
	u_setrt(rt_pl_quad, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h4, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w4, h4, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w4, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_blur_pl->E[1]); // Downsample from pl_half
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	// 1c: pl_quad (1/4) -> pl_octo (1/8)
	u_setrt(rt_pl_octo, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h8, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w8, h8, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w8, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_blur_pl->E[2]); // Downsample from pl_quad
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	// 1d: pl_octo (1/8) -> pl_hexa (1/16)
	u_setrt(rt_pl_hexa, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h16, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w16, h16, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w16, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_blur_pl->E[3]); // Downsample from pl_octo
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//////////////////////////////////////////////////////////////////////////////
	// Step 2: Copy hexa to vblur to start cascaded blur
	//////////////////////////////////////////////////////////////////////////////
	HW.pContext->CopyResource(rt_pl_vblur->pTexture->surface_get(), rt_pl_hexa->pTexture->surface_get());

	//////////////////////////////////////////////////////////////////////////////
	// Step 3: Cascaded Blur - matching FGFX pattern
	// Cascade 0 (offset 1): 4 times for smoothness
	// Cascade 1 (offset 3): 1 time
	// Cascade 2 (offset 9): 1 time, capture short blur, 1 more time
	// Cascade 0 (offset 1): 1 time (ultra-smooth)
	//////////////////////////////////////////////////////////////////////////////

	// Cascade 0: rectangular pass
	PL_BlurPass(this, 1.0f, radius, w16, h16, rt_pl_hblur, rt_pl_vblur, s_blur_pl, g_combine);

	// Cascade 0: smooth pass
	PL_BlurPass(this, 1.0f, radius, w16, h16, rt_pl_hblur, rt_pl_vblur, s_blur_pl, g_combine);

	// Cascade 0: super-smooth pass
	PL_BlurPass(this, 1.0f, radius, w16, h16, rt_pl_hblur, rt_pl_vblur, s_blur_pl, g_combine);

	// Cascade 1: rectangular pass
	PL_BlurPass(this, 3.0f, radius, w16, h16, rt_pl_hblur, rt_pl_vblur, s_blur_pl, g_combine);

	// Cascade 2: rectangular pass
	PL_BlurPass(this, 9.0f, radius, w16, h16, rt_pl_hblur, rt_pl_vblur, s_blur_pl, g_combine);

	//////////////////////////////////////////////////////////////////////////////
	// Step 4: Capture SHORT BLUR for recovery pass (this is the key timing!)
	// FGFX captures it here - after cascade 2 rectangular but before smooth
	//////////////////////////////////////////////////////////////////////////////
	HW.pContext->CopyResource(rt_pl_short->pTexture->surface_get(), rt_pl_vblur->pTexture->surface_get());

	// Cascade 2: smooth pass (after short blur capture)
	PL_BlurPass(this, 9.0f, radius, w16, h16, rt_pl_hblur, rt_pl_vblur, s_blur_pl, g_combine);

	// Cascade 0: ultra-smooth pass (final smoothing)
	PL_BlurPass(this, 1.0f, radius, w16, h16, rt_pl_hblur, rt_pl_vblur, s_blur_pl, g_combine);

	// Final LONG BLUR result is now in rt_pl_vblur

	//////////////////////////////////////////////////////////////////////////////
	// Step 5: Apply PL composite shader back to HW.pBaseRT
	//////////////////////////////////////////////////////////////////////////////
	u_setrt(Device.dwWidth, Device.dwHeight, HW.pBaseRT, NULL, NULL, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Set PL parameters for the composite shader
	RCache.set_c("pl_params", ps_r3_gi_pl_params);
	RCache.set_c("pl_params2", ps_r3_gi_pl_params2);

	RCache.set_Element(s_perceptual_lighting->E[0]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

void CRenderTarget::phase_ssfx_volumetric_blur()
{
	// Guard: rt_Generic_2 requires advancedpp to be created
	if (!rt_Generic_2 || !rt_ssfx_volumetric || !rt_ssfx_volumetric_tmp)
		return;

	// Be careful and clear the buffer ( rt_Generic_2 contain unspeakable stuff if no volumetric is written )
	if (!m_bHasActiveVolumetric)
	{
		FLOAT ColorRGBA[4] = { 0.0, 0.0, 0.0, 0.0 };
		HW.pContext->ClearRenderTargetView(rt_Generic_2->pRT, ColorRGBA);
	}

	if (!m_bHasActiveVolumetric_spot)
	{
		FLOAT ColorRGBA[4] = { 0.0, 0.0, 0.0, 0.0 };
		HW.pContext->ClearRenderTargetView(rt_ssfx_volumetric->pRT, ColorRGBA);
		return;
	}

	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	FVF::TL* pv;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);

	// Volumetric always at 1/8 res
	set_viewport_size(HW.pContext, w / 8, h / 8);

	ref_rt* rt_VolBlur[2] = { &rt_ssfx_volumetric_tmp, &rt_ssfx_volumetric };
	int pixelsize[4] = { 0, 1, 1, 2 }; // half pixel + pixelsize
	float pixelscale[4] = { 2.0f, 0.5f, 2.0f, 0.5f };

	// BLUR ///////////////////////////////////////////////////////////////////
	for (int b = 0; b < 4; b++)
	{
		u_setrt(*rt_VolBlur[b % 2], 0, 0, NULL);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		// Fill vertex buffer
		pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
		pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
		pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
		pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Draw COLOR
		RCache.set_Element(s_ssfx_volumetric_blur->E[b % 2]);
		RCache.set_c("blur_setup", w / 8, h / 8, pixelsize[b], pixelscale[b]);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	// Restore Viewport
	set_viewport_size(HW.pContext, w, h);

	// COMBINE ////////////////////////////////////////////////////////////////
	u_setrt(rt_ssfx_accum, 0, 0, NULL);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
	pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
	pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
	pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_volumetric_blur->E[5]);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	HW.pContext->CopyResource(rt_Generic_2->pTexture->surface_get(), rt_ssfx_accum->pTexture->surface_get());
};

void CRenderTarget::phase_ssfx_water_blur()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(0.5f, 0.5f);

	set_viewport_size(HW.pContext, w / 2, h / 2);

	if (ps_ssfx_water.y > 0)
	{
		// BLUR PHASE 1 //////////////////////////////////////////////////////////
		u_setrt(rt_ssfx_temp2, 0, 0, NULL);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		// Fill vertex buffer
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
		pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
		pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
		pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Draw COLOR
		RCache.set_Element(s_ssfx_water_blur->E[0]);
		RCache.set_c("blur_setup", 1, 0, 0, 2.0f / ps_ssfx_water.x);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

		// BLUR PHASE 2 //////////////////////////////////////////////////////////
		u_setrt(rt_ssfx_temp, 0, 0, NULL);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		// Fill vertex buffer
		pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
		pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
		pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
		pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Draw COLOR
		RCache.set_Element(s_ssfx_water_blur->E[1]);
		RCache.set_c("blur_setup", 0, 1, 0, 1.0f);

		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}
	else
	{
		HW.pContext->CopyResource(rt_ssfx_temp2->pTexture->surface_get(), rt_ssfx_temp->pTexture->surface_get());

		u_setrt(rt_ssfx_temp, 0, 0, NULL);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		// Fill vertex buffer
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
		pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
		pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
		pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Draw COLOR
		RCache.set_Element(s_ssfx_water_blur->E[2]);
		RCache.set_c("blur_setup", 0, 0, 0, 2.0f / ps_ssfx_water.x);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	set_viewport_size(HW.pContext, w, h);
	p1.set(1.0f, 1.0f);

};

void CRenderTarget::phase_ssfx_water_waves()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	u32 w = Device.dwWidth;
	u32 h = Device.dwHeight;


	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);

	set_viewport_size(HW.pContext, 512, 512);

	u_setrt(rt_ssfx_water_waves, 0, 0, NULL);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_water_blur->E[5]);
	RCache.set_c("wind_setup", g_pGamePersistent->Environment().wind_anim.w, g_pGamePersistent->Environment().CurrentEnv->wind_velocity, 0, 0);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	set_viewport_size(HW.pContext, w, h);
};

void CRenderTarget::phase_ssfx_sss()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(255, 255, 255, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);

	u_setrt(rt_ssfx, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_sss->E[0]);

	RCache.set_c("m_current", Matrix_current);
	RCache.set_c("m_previous", Matrix_previous);
	RCache.set_c("ssfx_sss", ps_ssfx_sss);

	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);


	// BLUR
	u_setrt(rt_ssfx_temp, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_sss->E[1]);

	RCache.set_c("blur_setup", 0, 1, 0, 0);

	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);


	// BLUR
	u_setrt(rt_ssfx_temp2, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_sss->E[2]);

	RCache.set_c("blur_setup", 1, 0, 2, 0);

	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);


	HW.pContext->CopyResource(rt_ssfx_sss->pTexture->surface_get(), rt_ssfx_temp2->pTexture->surface_get());

};

void CRenderTarget::phase_ssfx_sss_ext(light_Package& LP)
{
	static shared_str strLights("lights_data");
	static light* LightSlot[8];
	static u32 sss_currentframe;
	void* LightData;

	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(255, 255, 255, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = Device.dwWidth;
	float h = Device.dwHeight;

	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);

	u_setrt(rt_ssfx_sss_tmp, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_sss_ext->E[0]);

	RCache.set_c("m_current", Matrix_current);
	RCache.set_c("m_previous", Matrix_previous);
	RCache.set_c("ssfx_sss", ps_ssfx_sss);
	RCache.set_c("id_offset", 0);

	Fvector4* Lights_Array;
	{
		RCache.get_ConstantDirect(strLights, 4 * sizeof(Fvector4) * 2, 0, 0, &LightData);
		Lights_Array = (Fvector4*)LightData;
	}

	VERIFY(Lights_Array);

	if (Lights_Array)
	{
		for (int slot = 0; slot < 8; slot++)
			Lights_Array[slot].set(0, 0, 0, 0);

		xr_vector<light*> LightsSort;
		bool CheckPackage = true;

		if (Device.dwFrame > sss_currentframe)
		{
			sss_currentframe = Device.dwFrame + 2;

			xr_vector<light*>& source = LP.v_shadowed;
			for (u32 it = 0; it < source.size(); it++)
			{
				light* L = source[it];

				if (L->omnipart_num == 0 && L->range > 1.5f)
				{
					if (L->distance < 800 && L->flags.bActive)
					{
						L->distance_lpos = Device.vCameraPosition.distance_to(L->position);

						if (L->distance_lpos <= L->range)
							L->sss_priority = 0;
						else
							L->sss_priority = 1;

						LightsSort.push_back(L);
					}
				}

				// Refresh hierarchy ( Look for a better way? )
				if (L->sss_refresh)
				{
					L->sss_refresh = false;
					int done = 0;

					for (u32 lit = 0; lit < source.size(); lit++)
					{
						light* L2 = source[lit];
						if (L2->omipart_parent == L->omipart_parent)
						{
							L2->sss_id = L->sss_id;
							done++;
							if (done >= 6) break; // Update done.
						}
					}
				}
			}

			// Sort Distance
			std::sort(LightsSort.begin(), LightsSort.end(), SortLights);

			for (int x = 0; x < LightsSort.size(); x++)
			{
				light* L = LightsSort[x];

				bool Add = true;
				int FreeSlot = -1;

				for (int slot = 0; slot < 8; slot++)
				{
					if (LightSlot[slot])
					{
						if (LightSlot[slot] == L)
						{
							Add = false;
							break;
						}
					}
					else
						FreeSlot = slot;
				}

				if (Add && FreeSlot > -1)
				{
					LightSlot[FreeSlot] = L;

					L->sss_id = FreeSlot;

					if (L->flags.type == IRender_Light::OMNIPART)
						L->sss_refresh = true;
				}
			}
		}
		else
		{
			// Don't check the sorted package when the frame is skipped
			CheckPackage = false;
		}

		for (int slot = 0; slot < 8; slot++)
		{
			if (LightSlot[slot])
			{
				// Check if the light still exist on the sorted Light Package
				bool Remove = true;

				if (CheckPackage)
				{
					for (int x = 0; x < LightsSort.size(); x++)
					{
						light* L = LightsSort[x];

						if (L == LightSlot[slot])
							Remove = false;
					}
				}
				else
				{
					// The distance calc was skipped, check here instead
					LightSlot[slot]->distance_lpos = Device.vCameraPosition.distance_to(LightSlot[slot]->position);
					Remove = false;
				}

				float Dist = LightSlot[slot]->distance_lpos;

				if (Dist > (LightSlot[slot]->range * 2.0f))
					Remove = true;

				// Remove Light
				if (!LightSlot[slot]->flags.bActive || Remove)
				{
					if (LightSlot[slot]->flags.type == IRender_Light::OMNIPART)
						LightSlot[slot]->sss_refresh = true;

					LightSlot[slot]->sss_id = -1;
					LightSlot[slot] = NULL;
				}
				else
				{
					// Update Light
					Fvector L_pos;

					Device.mView.transform_tiny(L_pos, LightSlot[slot]->position);

					// Distance Atte ( Use MaxAtte if the light range is bigger than the max sort range )
					float MaxAtte = 1.0f - (clampr((LightSlot[slot]->distance - 780) / -100, 0.f, 1.f));
					float Atte = 1.0f - (clampr((Dist - LightSlot[slot]->range * 1.9f) / -(LightSlot[slot]->range / 2.0f), 0.f, 1.f));

					// ( Reminder ) The value is inverted ( 1.0 = Fadeout ~ 0.0 = Full Visible )
					Lights_Array[slot].set(L_pos.x, L_pos.y, L_pos.z, std::max(MaxAtte, Atte));

				}
			}
		}
	}

	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	HW.pContext->CopyResource(rt_ssfx_sss_ext->pTexture->surface_get(), rt_ssfx_sss_tmp->pTexture->surface_get());

	// SSS Ext 2 -------------------------------------------------------

	u_setrt(rt_ssfx_sss_tmp, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_sss_ext->E[1]);

	RCache.set_c("m_current", Matrix_current);
	RCache.set_c("m_previous", Matrix_previous);
	RCache.set_c("id_offset", 1);
	RCache.get_ConstantDirect(strLights, 4 * sizeof(Fvector4) * 2, 0, 0, &LightData);

	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	HW.pContext->CopyResource(rt_ssfx_sss_ext2->pTexture->surface_get(), rt_ssfx_sss_tmp->pTexture->surface_get());

	// Combine ---------------------------------------------------------

	u_setrt(rt_ssfx_sss_tmp, nullptr, nullptr, nullptr);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(w, h, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(w, 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_sss_ext->E[2]);

	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}


void CRenderTarget::phase_ssfx_fog_scattering()
{
	u32 Offset = 0;
	Fvector2 p0, p1;

	u32 C = color_rgba(255, 255, 255, 255);
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);

	FVF::TL* pv;

	ref_rt* rt_Blur[2] = {&rt_blur_4, &rt_blur_2};
	
	for (int blurp = 0; blurp < 2; blurp++)
	{
		int SampleScale = 1 << (2 - blurp); // 0 = 4 -> 1 = 2

		set_viewport_size(HW.pContext, w / SampleScale, h / SampleScale);

		u_setrt(*rt_Blur[blurp], 0, 0, NULL);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);

		// Fill vertex buffer
		pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
		pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
		pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
		pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Draw COLOR
		RCache.set_Element(s_ssfx_fog_scattering->E[2 + blurp]);
		RCache.set_c("blur_setup", w / SampleScale, h / SampleScale, 0, 0);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	set_viewport_size(HW.pContext, w, h);

	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;

	// Fog Scattering
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, h, EPS_S, 1.0f, C, 0.0f, 1.0f); pv++;
	pv->set(0, 0, EPS_S, 1.0f, C, 0.0f, 0.0f); pv++;
	pv->set(w, h, EPS_S, 1.0f, C, 1.0f, 1.0f); pv++;
	pv->set(w, 0, EPS_S, 1.0f, C, 1.0f, 0.0f); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Draw COLOR
	RCache.set_Element(s_ssfx_fog_scattering->E[0]);
	RCache.set_c("ssfx_scattering_setup", ps_ssfx_fog_scattering,0,0,0);

	RCache.set_Geometry(g_combine);

	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());

}

#endif
