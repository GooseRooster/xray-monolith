#include "stdafx.h"

// OWA: XeGTAO Compute Shader - Intel's Ground Truth Ambient Occlusion
// Two-pass compute implementation:
// Pass 1 (Main): 8x8 thread groups compute GTAO visibility + bent normals + edges
// Pass 2 (Denoise): Edge-aware spatial blur when TAA is disabled
//
// Compute advantages over previous pixel shader path:
// - Eliminates CopyResource (compute reads/writes distinct textures)
// - Enables groupshared depth tile preloading (reduced redundant fetches)
// - Enables future async compute overlap with other passes

void CRenderTarget::phase_xegtao()
{
	u32 w = Device.dwWidth;
	u32 h = Device.dwHeight;

	// Thread group dimensions — 8x8 threads per group
	u32 groupsX = (w + 7) / 8;
	u32 groupsY = (h + 7) / 8;

	bool use_denoise = !RImplementation.o.ssfx_taa;

	// ============================================
	// Unbind render targets before compute dispatch
	// DX11 requires OM targets unbound when binding UAVs
	// ============================================
	ID3D11RenderTargetView* nullRTVs[4] = { nullptr, nullptr, nullptr, nullptr };
	HW.pContext->OMSetRenderTargets(4, nullRTVs, nullptr);

	// ============================================
	// PASS 1: Main XeGTAO computation (compute shader)
	// Outputs via UAV: rt_gtao or rt_gtao_temp (bent normal + obscurance), rt_gtao_edges
	// When denoise is active: write to rt_gtao_temp (denoise reads it, writes rt_gtao)
	// When no denoise (TAA): write directly to rt_gtao (combine reads rt_gtao)
	// ============================================
	{
		ShaderElement* S = &*(s_xegtao->E[0]);
		SPass& P = *(S->passes[0]);
		RCache.set_States(P.state);
		RCache.set_Constants(P.constants);
		RCache.set_Textures(P.T);
		RCache.set_CS(P.cs);

		// Constants
		u32 frame_index = Device.dwFrame;
		if (Device.m_SecondViewport.IsSVPFrame())
			frame_index = (Device.dwFrame > 0) ? (Device.dwFrame - 1) : 0;
		u32 temporal_index = frame_index % 64;

		RCache.set_c("xegtao_params", float(temporal_index), 0.0f, 0.0f, 0.0f);
		RCache.set_c("m_v2w", Device.mInvView);
		RCache.set_c("resolution", float(w), float(h), 1.0f / float(w), 1.0f / float(h));

		float fov_tan = tanf(deg2rad(Device.fFOV) * 0.5f);
		float aspect = Device.fASPECT;
		float near_plane = VIEWPORT_NEAR;
		float far_plane = g_pGamePersistent->Environment().CurrentEnv->far_plane;
		RCache.set_c("xegtao_proj", fov_tan, fov_tan * aspect, near_plane, far_plane);

		// Bind UAV outputs
		// u0 = main AO output, u1 = edge data
		UINT uavInit = 0;
		ID3D11UnorderedAccessView* uavs[2] = {
			use_denoise ? rt_gtao_temp->pUAView : rt_gtao->pUAView,
			rt_gtao_edges->pUAView
		};
		HW.pContext->CSSetUnorderedAccessViews(0, 2, uavs, &uavInit);

		// Dispatch
		RCache.Compute(groupsX, groupsY, 1);

		// Unbind UAVs
		ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
		HW.pContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, &uavInit);
	}

	// Unbind CS SRVs to prevent resource conflicts between passes
	ID3D11ShaderResourceView* nullSRVs[16] = {};
	HW.pContext->CSSetShaderResources(0, 16, nullSRVs);

	// ============================================
	// PASS 2: Edge-aware denoise (optional — skip if TAA enabled)
	// Reads rt_gtao_temp (SRV, bound in blender) + rt_gtao_edges (SRV)
	// Writes to rt_gtao (UAV) — no CopyResource needed!
	// ============================================
	if (use_denoise)
	{
		ShaderElement* S = &*(s_xegtao->E[1]);
		SPass& P = *(S->passes[0]);
		RCache.set_States(P.state);
		RCache.set_Constants(P.constants);
		RCache.set_Textures(P.T);  // s_gtao = rt_gtao_temp (SRV), s_gtao_edges (SRV)
		RCache.set_CS(P.cs);

		RCache.set_c("xegtao_denoise_params", 1.2f, 0.0f, 0.0f, 0.0f);
		RCache.set_c("resolution", float(w), float(h), 1.0f / float(w), 1.0f / float(h));

		// Bind UAV output — denoised result goes to rt_gtao
		UINT uavInit = 0;
		ID3D11UnorderedAccessView* uav[1] = { rt_gtao->pUAView };
		HW.pContext->CSSetUnorderedAccessViews(0, 1, uav, &uavInit);

		// Dispatch
		RCache.Compute(groupsX, groupsY, 1);

		// Unbind UAV
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		HW.pContext->CSSetUnorderedAccessViews(0, 1, nullUAV, &uavInit);
	}

	// Clean up CS state
	HW.pContext->CSSetShaderResources(0, 16, nullSRVs);
	HW.pContext->CSSetShader(nullptr, nullptr, 0);
}
