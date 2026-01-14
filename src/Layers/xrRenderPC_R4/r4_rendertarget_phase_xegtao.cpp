#include "stdafx.h"

// OWA: XeGTAO - Intel's Ground Truth Ambient Occlusion
// Two-pass implementation:
// Pass 1 (Main): Computes GTAO visibility + bent normals, outputs edge data for denoise
// Pass 2 (Denoise): Edge-aware spatial blur when TAA is disabled

void CRenderTarget::phase_xegtao()
{
	u32 Offset = 0;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	// Clear the GTAO render targets
	FLOAT ColorRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	HW.pContext->ClearRenderTargetView(rt_gtao->pRT, ColorRGBA);
	HW.pContext->ClearRenderTargetView(rt_gtao_edges->pRT, ColorRGBA);

	// ============================================
	// PASS 1: Main XeGTAO computation
	// Outputs: rt_gtao (RGB=bent normal, A=obscurance), rt_gtao_edges (packed edges)
	// ============================================
	u_setrt(rt_gtao, rt_gtao_edges, 0);
	RCache.set_CullMode(D3DCULL_NONE);
	RCache.set_Stencil(FALSE);

	// Fill vertex buffer for fullscreen quad
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
	pv->set(-1,  1, 0, 1, 0, 0, 1);  pv++;
	pv->set(-1, -1, 0, 0, 0, 0, 0);  pv++;
	pv->set( 1,  1, 1, 1, 0, 1, 1);  pv++;
	pv->set( 1, -1, 1, 0, 0, 1, 0);  pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// Set shader element 0 (main pass)
	RCache.set_Element(s_xegtao->E[0]);
	RCache.set_Geometry(g_combine);

	// Pass constants to shader
	// xegtao_params: x = temporal_index for spatio-temporal noise
	//
	// Use Device.dwFrame instead of a static counter to ensure temporal consistency.
	// The Hilbert + R2 sequence expects coherent frame-to-frame progression.
	// A static counter can get out of sync with actual frame rendering (e.g., SecondVP
	// scope rendering causes multiple phase_xegtao calls per logical frame).
	u32 frame_index = Device.dwFrame;

	// Handle SecondVP: use previous frame's index for scope viewport frames
	// This maintains temporal coherence between main viewport and scope rendering,
	// matching how other temporal effects (exposure, etc.) handle SVP in phase_combine
	if (Device.m_SecondViewport.IsSVPFrame())
	{
		frame_index = (Device.dwFrame > 0) ? (Device.dwFrame - 1) : 0;
	}

	u32 temporal_index = frame_index % 64;  // Cycle through 64 frames for R2 sequence

	RCache.set_c("xegtao_params", float(temporal_index), 0.0f, 0.0f, 0.0f);
	RCache.set_c("m_v2w", Device.mInvView);
	RCache.set_c("resolution", w, h, 1.0f / w, 1.0f / h);

	// Projection matrix constants for depth linearization
	float fov_tan = tanf(deg2rad(Device.fFOV) * 0.5f);
	float aspect = Device.fASPECT;
	float near_plane = VIEWPORT_NEAR;
	float far_plane = g_pGamePersistent->Environment().CurrentEnv->far_plane;
	RCache.set_c("xegtao_proj", fov_tan, fov_tan * aspect, near_plane, far_plane);

	// Render main pass
	if (!RImplementation.o.dx10_msaa)
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	else
	{
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
		// MSAA handling would go here if needed
	}

	// ============================================
	// PASS 2: Edge-aware denoise (optional - skip if TAA is enabled)
	// TAA provides temporal accumulation that replaces spatial blur
	// ============================================
	bool use_taa = RImplementation.o.ssfx_taa; // Check if TAA is enabled

	if (!use_taa)
	{
		// Copy rt_gtao to rt_gtao_temp to avoid read/write hazard
		// (denoise shader reads from temp, writes to rt_gtao)
		HW.pContext->CopyResource(rt_gtao_temp->pTexture->surface_get(), rt_gtao->pTexture->surface_get());

		// Denoise pass - blur while respecting edges
		u_setrt(rt_gtao, 0, 0, 0);  // Output back to rt_gtao
		RCache.set_CullMode(D3DCULL_NONE);
		RCache.set_Stencil(FALSE);

		// Fill vertex buffer for fullscreen quad
		pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, Offset);
		pv->set(-1,  1, 0, 1, 0, 0, 1);  pv++;
		pv->set(-1, -1, 0, 0, 0, 0, 0);  pv++;
		pv->set( 1,  1, 1, 1, 0, 1, 1);  pv++;
		pv->set( 1, -1, 1, 0, 0, 1, 0);  pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);

		// Set shader element 1 (denoise pass)
		RCache.set_Element(s_xegtao->E[1]);
		RCache.set_Geometry(g_combine);

		// Denoise parameters
		// blur_beta controls edge sensitivity (higher = more blur, less edge-aware)
		float blur_beta = 1.2f;  // Tunable parameter
		RCache.set_c("xegtao_denoise_params", blur_beta, 0.0f, 0.0f, 0.0f);
		RCache.set_c("resolution", w, h, 1.0f / w, 1.0f / h);

		// Render denoise pass
		RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
	}

	RCache.set_Stencil(FALSE);

	// Explicitly unbind render targets before returning
	// This ensures rt_gtao is not bound as RTV when subsequent passes (IL, combine_1)
	// try to sample it as SRV, preventing potential DX11 resource conflicts
	RCache.set_RT(NULL, 0);
	RCache.set_RT(NULL, 1);
}
