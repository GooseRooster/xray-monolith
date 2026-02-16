#include "stdafx.h"
#include "../xrRender/LightProbeGrid.h"

extern dx10ShaderResourceStateCache SRVSManager;

// OWA: Probe Volume Sparse Update — Compute Shader Dispatch
//
// Reads a structured buffer of dirty voxel data (filled by LightProbeGrid::PrepareVolumeUpdate)
// and scatters each entry into the 3 volume Texture3D UAVs. Typical frame: ~1000 updates (~64KB)
// vs the old MAP_WRITE_DISCARD path that uploaded the entire 24MB volume every frame.
//
// Called from phase_combine() before volume textures are bound as SRVs for combine_1.ps.

void CRenderTarget::phase_probe_volume_update()
{
	if (!g_LightProbeGrid || !g_LightProbeGrid->HasPendingUpdate())
		return;

	u32 updateCount = g_LightProbeGrid->GetPendingUpdateCount();
	if (updateCount == 0) return;

	ID3D11ShaderResourceView* updateSRV = g_LightProbeGrid->GetUpdateSRV();
	if (!updateSRV) return;

	ID3D11UnorderedAccessView* uav0 = g_LightProbeGrid->GetVolumeUAV(0);
	ID3D11UnorderedAccessView* uav1 = g_LightProbeGrid->GetVolumeUAV(1);
	ID3D11UnorderedAccessView* uav2 = g_LightProbeGrid->GetVolumeUAV(2);
	if (!uav0 || !uav1 || !uav2) return;

	// Unbind render targets before compute dispatch (DX11 requirement)
	ID3D11RenderTargetView* nullRTVs[4] = { nullptr, nullptr, nullptr, nullptr };
	HW.pContext->OMSetRenderTargets(4, nullRTVs, nullptr);

	// Activate compute shader from blender
	ShaderElement* S = &*(s_probe_volume_cs->E[0]);
	SPass& P = *(S->passes[0]);
	RCache.set_States(P.state);
	RCache.set_Constants(P.constants);
	RCache.set_CS(P.cs);

	// Set constant: update count
	RCache.set_c("probe_volume_params", (float)updateCount, 0.f, 0.f, 0.f);

	// Bind structured buffer SRV via engine's SRV manager (slot t0)
	// Using SRVSManager ensures proper state tracking across compute passes.
	SRVSManager.SetCSResource(0, updateSRV);

	// Bind volume UAVs (no engine manager for UAVs — manual DX11 calls)
	UINT uavInit = (UINT)-1;
	ID3D11UnorderedAccessView* uavs[3] = { uav0, uav1, uav2 };
	HW.pContext->CSSetUnorderedAccessViews(0, 3, uavs, &uavInit);

	// Dispatch: 64 threads per group, one update per thread
	u32 groups = (updateCount + 63) / 64;
	RCache.Compute(groups, 1, 1);

	// Cleanup: unbind UAVs and CS SRVs
	ID3D11UnorderedAccessView* nullUAVs[3] = { nullptr, nullptr, nullptr };
	HW.pContext->CSSetUnorderedAccessViews(0, 3, nullUAVs, &uavInit);
	ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
	HW.pContext->CSSetShaderResources(0, 1, nullSRVs);
	SRVSManager.SetCSResource(0, nullptr);  // Keep manager in sync

	// Mark dispatch complete
	g_LightProbeGrid->ClearPendingUpdate();
}
