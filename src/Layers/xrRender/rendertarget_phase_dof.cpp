#include "stdafx.h"

void CRenderTarget::phase_dof()
{
	//Constants
	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);

	float d_Z = EPS_S;
	float d_W = 1.0f;
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

	//DoF vectors
	Fvector2 vDofKernel;
	vDofKernel.set(0.5f / Device.dwWidth, 0.5f / Device.dwHeight);
	vDofKernel.mul(ps_r2_dof_kernel_size);
	Fvector3 dof;
	g_pGamePersistent->GetCurrentDof(dof);

	//////////////////////////////////////////////////////////////////////////
	//Set MSAA/NonMSAA rendertarget
	u_setrt(rt_dof, 0, 0, HW.pBaseZB);

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_dof->E[0]);

	//Set paramterers
	RCache.set_c("dof_params", dof.x, dof.y, dof.z, ps_r2_dof_sky);
	RCache.set_c("dof_kernel", vDofKernel.x, vDofKernel.y, ps_r2_dof_kernel_size, ps_r2_dof_peripheral_softness);
	float dof_blend = g_pGamePersistent->GetDofBlendFactor();
	RCache.set_c("dof_control", ps_r2_dof_max_blur, ps_r2_dof_coc_power, dof_blend, 0.f);
	
	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	////////////////////////////////////////////////////////////////////////////
#if defined(USE_DX10) || defined(USE_DX11)
	ref_rt& dest_rt = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;
	u_setrt(dest_rt, nullptr, nullptr, nullptr);
#else
	u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
#endif		

	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	//Fill vertex buffer
	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, float(h), d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0, d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(float(w), float(h), d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(float(w), 0, d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	//Set pass
	RCache.set_Element(s_dof->E[1]);

	//Set geometry
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//Resolve RT
#if defined(USE_DX10) || defined(USE_DX11)
	HW.pContext->CopyResource(rt_Generic_0->pTexture->surface_get(), dest_rt->pTexture->surface_get());
#endif
};

// OWA: Kawase downsample pyramid for DOF
// 3 passes: full->half, half->quarter, quarter->eighth
// Reuses rt_blur_h_* targets (dead after phase_blur() completes)
// First pass adds bloom contribution so defocused lights carry their glow
void CRenderTarget::phase_dof_blur()
{
	u32 Offset = 0;
	float d_Z = EPS_S;
	float d_W = 1.0f;
	u32 C = color_rgba(0, 0, 0, 255);

	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	Fvector2 p0, p1;
	p0.set(0.0f, 0.0f);
	p1.set(1.0f, 1.0f);

	FVF::TL* pv;

	// Bloom additive weight for first pass (subtle contribution)
	float bloom_weight = 0.3f;

	//////////////////////////////////////////////////////////////////////////
	// Pass 0: Full-res (generic0) -> half-res (blur_h_2), with bloom
	//////////////////////////////////////////////////////////////////////////
	float tw = w * 0.5f;
	float th = h * 0.5f;

	u_setrt(rt_blur_h_2, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, th, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0,  d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(tw, th, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(tw, 0,  d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_dof_blur->E[0]);
	RCache.set_c("dof_blur_params", 1.0f / w, 1.0f / h, bloom_weight, 0.0f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//////////////////////////////////////////////////////////////////////////
	// Pass 1: Half-res (blur_h_2) -> quarter-res (blur_h_4)
	//////////////////////////////////////////////////////////////////////////
	tw = w * 0.25f;
	th = h * 0.25f;

	u_setrt(rt_blur_h_4, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, th, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0,  d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(tw, th, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(tw, 0,  d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_dof_blur->E[1]);
	RCache.set_c("dof_blur_params", 1.0f / (w * 0.5f), 1.0f / (h * 0.5f), 0.0f, 0.0f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//////////////////////////////////////////////////////////////////////////
	// Pass 2: Quarter-res (blur_h_4) -> eighth-res (blur_h_8)
	//////////////////////////////////////////////////////////////////////////
	tw = w * 0.125f;
	th = h * 0.125f;

	u_setrt(rt_blur_h_8, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, th, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0,  d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(tw, th, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(tw, 0,  d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_dof_blur->E[2]);
	RCache.set_c("dof_blur_params", 1.0f / (w * 0.25f), 1.0f / (h * 0.25f), 0.0f, 0.0f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//////////////////////////////////////////////////////////////////////////
	// Pass 3: Upsample eighth-res (blur_h_8) -> quarter-res (blur_4)
	//////////////////////////////////////////////////////////////////////////
	tw = w * 0.25f;
	th = h * 0.25f;

	u_setrt(rt_blur_4, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, th, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0,  d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(tw, th, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(tw, 0,  d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_dof_blur->E[3]);
	RCache.set_c("dof_blur_params", 8.0f / w, 8.0f / h, 0.0f, 0.0f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

	//////////////////////////////////////////////////////////////////////////
	// Pass 4: Upsample quarter-res (blur_4) -> half-res (blur_2)
	//////////////////////////////////////////////////////////////////////////
	tw = w * 0.5f;
	th = h * 0.5f;

	u_setrt(rt_blur_2, 0, 0, 0);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(0, th, d_Z, d_W, C, p0.x, p1.y); pv++;
	pv->set(0, 0,  d_Z, d_W, C, p0.x, p0.y); pv++;
	pv->set(tw, th, d_Z, d_W, C, p1.x, p1.y); pv++;
	pv->set(tw, 0,  d_Z, d_W, C, p1.x, p0.y); pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	RCache.set_Element(s_dof_blur->E[4]);
	RCache.set_c("dof_blur_params", 4.0f / w, 4.0f / h, 0.0f, 0.0f);
	RCache.set_Geometry(g_combine);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}
