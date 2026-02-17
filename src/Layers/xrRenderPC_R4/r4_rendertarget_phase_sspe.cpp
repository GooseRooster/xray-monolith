#include "stdafx.h"
#include "../xrRender/LightProbeGrid.h"

// OWA: Screen-Space Probe Enhancement (SSPE) — Compute Shader Dispatch
//
// 8-sample structured screen-space color bounce with lightweight temporal smoothing.
// Reads G-buffer position (depth + normals) and previous frame's composited scene
// (rt_Generic_0 — still contains last frame's output at SSPE dispatch time).
// Writes to one of two ping-pong RTs (half-res RGBA16F: RGB = bounce color, A = confidence).
//
// Ping-pong double-buffer: eliminates CopyResource by alternating write/read RTs each frame.
// Even frames: write rt_sspe, read rt_sspe_prev
// Odd frames:  write rt_sspe_prev, read rt_sspe
// After dispatch, rt_sspe->pTexture SRV is redirected to the just-written surface for combine_1.
//
// Called from phase_combine() after phase_probe_volume_update(), before combine_1 draw.

extern float ps_r_sspe_radius;
extern float ps_r_sspe_intensity;
extern float ps_r_sspe_max_distance;

void CRenderTarget::phase_sspe()
{
	// Guard: probes must be active
	if (!g_LightProbeGrid || g_LightProbeGrid->GetProbeCount() == 0)
		return;

	// Skip if intensity is zero (effectively disabled)
	if (ps_r_sspe_intensity <= 0.001f)
		return;

	u32 w = Device.dwWidth / 2;
	u32 h = Device.dwHeight / 2;

	// Thread group dimensions — 8x8 threads per group at half-resolution
	u32 groupsX = (w + 7) / 8;
	u32 groupsY = (h + 7) / 8;

	// ============================================
	// Ping-pong: determine write/read RTs
	// Even frames: write rt_sspe, read rt_sspe_prev
	// Odd frames:  write rt_sspe_prev, read rt_sspe
	// ============================================
	u32 write_idx = Device.dwFrame & 1;
	ref_rt& rt_write = write_idx ? rt_sspe_prev : rt_sspe;
	ref_rt& rt_read  = write_idx ? rt_sspe : rt_sspe_prev;

	// ============================================
	// Unbind render targets before compute dispatch
	// DX11 requires OM targets unbound when binding UAVs
	// ============================================
	ID3D11RenderTargetView* nullRTVs[4] = { nullptr, nullptr, nullptr, nullptr };
	HW.pContext->OMSetRenderTargets(4, nullRTVs, nullptr);

	// ============================================
	// Set "read" surface on s_sspe_prev CTexture
	// The blender maps s_sspe_prev → rt_sspe_prev->pTexture.
	// Override its SRV to point to whichever surface is the "read" this frame.
	// ============================================
	rt_sspe_prev->pTexture->surface_set(
		reinterpret_cast<ID3DBaseTexture*>(rt_read->pSurface));

	// ============================================
	// Activate compute shader from blender
	// ============================================
	ShaderElement* S = &*(s_sspe->E[0]);
	SPass& P = *(S->passes[0]);
	RCache.set_States(P.state);
	RCache.set_Constants(P.constants);
	// Invalidate CS texture cache to prevent stale entries from prior CS phases
	// (XeGTAO → probe volume cleanup nullifies SRVSManager but not textures_cs[])
	RCache.InvalidateCSTextureCache();
	RCache.set_Textures(P.T);
	RCache.set_CS(P.cs);

	// ============================================
	// Constants
	// Auto-bound constants do NOT work for compute shaders in this engine —
	// must be set explicitly via set_c() (same pattern as XeGTAO phase)
	// ============================================

	// Screen dimensions (full-res) — used by shader to map half-res → full-res G-buffer coordinates
	RCache.set_c("screen_res", (float)Device.dwWidth, (float)Device.dwHeight,
		1.0f / (float)Device.dwWidth, 1.0f / (float)Device.dwHeight);

	// Position decompression — used by reconstruct_position() in the shader
	float VertTan = -1.0f * tanf(deg2rad(Device.fFOV / 2.0f));
	float HorzTan = -VertTan / Device.fASPECT;
	RCache.set_c("pos_decompression_params", HorzTan, VertTan,
		(2.0f * HorzTan) / (float)Device.dwWidth,
		(2.0f * VertTan) / (float)Device.dwHeight);
	RCache.set_c("pos_decompression_params2", (float)Device.dwWidth, (float)Device.dwHeight,
		1.0f / (float)Device.dwWidth, 1.0f / (float)Device.dwHeight);

	// SSPE parameters: x = radius (world-space meters), y = intensity, z = frame_index, w = max distance
	u32 frame_index = Device.dwFrame % 8;
	RCache.set_c("sspe_params", ps_r_sspe_radius, ps_r_sspe_intensity,
		(float)frame_index, ps_r_sspe_max_distance);

	// Half-resolution dimensions for UAV addressing
	RCache.set_c("sspe_half_res", (float)w, (float)h, 1.0f / (float)w, 1.0f / (float)h);

	// Camera reprojection matrices for temporal accumulation
	// SSFX motion vectors only encode per-object velocity — camera motion must be
	// computed via depth reprojection using current/previous projection matrices.
	// Same technique as motion blur (mblur.h) and TAA.
	{
		static Fmatrix s_prev_full_transform;
		static bool    s_has_prev = false;

		// m_previous: current-view-space → prev-clip-space
		// = prev_world_to_clip * curr_view_to_world
		Fmatrix m_reproj;
		if (s_has_prev)
			m_reproj.mul(s_prev_full_transform, Device.mInvView);
		else
			m_reproj.set(Device.mProject); // First frame: identity (no velocity)

		// m_current: current-view-space → current-clip-space
		RCache.set_c("m_current", Device.mProject);
		RCache.set_c("m_previous", m_reproj);

		// Save current full transform for next frame
		// Skip on SecondViewport frames (scope/PDA) to avoid contaminating history
		if (!Device.m_SecondViewport.IsSVPFrame())
		{
			s_prev_full_transform.set(Device.mFullTransform);
			s_has_prev = true;
		}
	}

	// ============================================
	// Bind write RT's UAV
	// ============================================
	UINT uavInit = 0;
	ID3D11UnorderedAccessView* uavs[1] = { rt_write->pUAView };
	HW.pContext->CSSetUnorderedAccessViews(0, 1, uavs, &uavInit);

	// ============================================
	// Dispatch compute shader
	// ============================================
	RCache.Compute(groupsX, groupsY, 1);

	// ============================================
	// Cleanup: unbind UAVs and CS SRVs
	// ============================================
	ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
	HW.pContext->CSSetUnorderedAccessViews(0, 1, nullUAV, &uavInit);
	ID3D11ShaderResourceView* nullSRVs[16] = {};
	HW.pContext->CSSetShaderResources(0, 16, nullSRVs);
	HW.pContext->CSSetShader(nullptr, nullptr, 0);

	// ============================================
	// Update s_sspe CTexture for combine_1
	// Point it to whichever surface was just WRITTEN, so combine_1 reads fresh data
	// ============================================
	rt_sspe->pTexture->surface_set(
		reinterpret_cast<ID3DBaseTexture*>(rt_write->pSurface));
	RCache.InvalidatePSTextureCacheFor(&*rt_sspe->pTexture);

	// NO CopyResource — ping-pong eliminates the need for copies
}
