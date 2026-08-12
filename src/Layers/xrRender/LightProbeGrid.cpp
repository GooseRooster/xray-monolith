#include "stdafx.h"
#include "LightProbeGrid.h"
#include "xrRender_console.h"
#include "r__sector.h"
#include "../../xrEngine/xr_object.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/IGame_Level.h"
#include "../../xrEngine/Environment.h"
#include "../../xrCore/_thread_types.h"

// =========================================================================
// Thread-local working buffers for UpdateProbe
//
// These replace the former per-instance members (m_collider).
// Using thread_local makes UpdateProbe safe to call from xr_parallel_for during
// Build() — each worker thread gets its own isolated instances. The main thread's
// instances are reused for the single-threaded runtime Update() path with zero
// overhead change.
//
// CDB::MODEL is read-only after level load, so concurrent ray_query calls on
// separate COLLIDER instances are safe (they share the BVH but never write it).
// =========================================================================

thread_local static CDB::COLLIDER               s_tl_collider;

// External console variables
extern int   ps_r_probe_update_rate;
extern int   ps_r_debug_probes;
extern int   ps_r_probe_upload_rate;
extern int   ps_r3_ssfx_il;

// Global instance
CLightProbeGrid* g_LightProbeGrid = nullptr;

// Golden ratio constant for temporal rotation of ray patterns.
// Each probe update rotates the Fibonacci ray pattern by 1/phi, giving a
// low-discrepancy sequence that maximally fills the hemisphere over time.
// After N updates, a probe has effectively sampled N*RAYS_PER_PROBE unique
// directions — dramatically finer resolution than the per-frame ray count alone.
static const float s_goldenRatio = 1.6180339887f;     // phi = (1 + sqrt(5)) / 2
static const float s_invGoldenRatio = 0.6180339887f;  // 1/phi

//////////////////////////////////////////////////////////////////////////
// CLightProbeGrid Implementation
//////////////////////////////////////////////////////////////////////////

CLightProbeGrid::CLightProbeGrid()
    : m_pProbeTexture(nullptr)
    , m_pProbeSRV(nullptr)
    , m_gpuBufferDirty(true)
    , m_gpuAllocatedProbes(0)
    , m_gpuCacheRowPitch(0)
    , m_gpuCacheTexHeight(0)
    , m_updateBudget(50)
    , m_currentFrame(0)
    , m_lastUpdateTimeMs(0)
    , m_debugEnabled(false)
    , m_hashCellSize(DEFAULT_HASH_CELL_SIZE)
    , m_lastUploadFrame(0)
    , m_lastGameTime(-1.0f)
    , m_temporalBlend(0.3f)
    , m_currentEnvLum(1.0f)
    , m_voxelSize(OUTDOOR_GRID_SPACING)
    , m_volDirty(true)
    , m_pUpdateBuffer(nullptr)
    , m_pUpdateSRV(nullptr)
    , m_dirtyVoxelCount(0)
    , m_pendingUpdateCount(0)
    , m_needFullUpload(false)
    , m_frustumRobinIndex(0)
    , m_bgRobinIndex(0)
    , m_budgetBoostFramesLeft(0)
{
    m_boundsMin.set(0, 0, 0);
    m_boundsMax.set(0, 0, 0);
    m_gridDims.set(0, 0, 0);
    m_hashMin.set(0, 0, 0);
    m_hashDims.set(0, 0, 0);
    m_volDims.set(0, 0, 0);
    m_volMin.set(0, 0, 0);
    m_volSize.set(0, 0, 0);
    m_prevCameraDir.set(0, 0, 1);

    for (int i = 0; i < NUM_VOLUME_TEXTURES; i++)
    {
        m_pVolTexture[i] = nullptr;
        m_pVolSRV[i] = nullptr;
        m_pVolUAV[i] = nullptr;
    }

}

CLightProbeGrid::~CLightProbeGrid()
{
    Clear();
}

void CLightProbeGrid::Clear()
{
    m_probes.clear();
    m_spatialHash.clear();
    m_gpuCache.clear();
    m_gpuCacheRowPitch = 0;
    m_gpuCacheTexHeight = 0;
    m_frustumProbes.clear();
    m_volAccum.clear();
    for (int i = 0; i < NUM_VOLUME_TEXTURES; i++)
        m_volData[i].clear();
    m_voxelDirtyFlags.clear();
    m_dirtyVoxelCount = 0;
    m_pendingUpdateCount = 0;
    m_needFullUpload = false;

    if (m_pProbeSRV)
    {
        m_pProbeSRV->Release();
        m_pProbeSRV = nullptr;
    }
    if (m_pProbeTexture)
    {
        m_pProbeTexture->Release();
        m_pProbeTexture = nullptr;
    }

    // Release sparse update structured buffer
    if (m_pUpdateSRV)
    {
        m_pUpdateSRV->Release();
        m_pUpdateSRV = nullptr;
    }
    if (m_pUpdateBuffer)
    {
        m_pUpdateBuffer->Release();
        m_pUpdateBuffer = nullptr;
    }

    // Release volume textures
    for (int i = 0; i < NUM_VOLUME_TEXTURES; i++)
    {
        if (m_pVolUAV[i])
        {
            m_pVolUAV[i]->Release();
            m_pVolUAV[i] = nullptr;
        }
        if (m_pVolSRV[i])
        {
            m_pVolSRV[i]->Release();
            m_pVolSRV[i] = nullptr;
        }
        if (m_pVolTexture[i])
        {
            m_pVolTexture[i]->Release();
            m_pVolTexture[i] = nullptr;
        }
    }

    m_gpuAllocatedProbes = 0;
    m_gpuBufferDirty = true;
    m_volDirty = true;
    m_frustumRobinIndex = 0;
    m_bgRobinIndex = 0;
    m_budgetBoostFramesLeft = 0;
    m_lastGameTime = -1.0f;
    m_temporalBlend = 0.3f;
    m_currentEnvLum = 1.0f;
    m_prevCameraDir.set(0, 0, 1);
}

bool CLightProbeGrid::IsPositionIndoor(const Fvector& pos)
{
    if (!g_pGameLevel) return false;

    CDB::MODEL* staticModel = g_pGameLevel->ObjectSpace.GetStaticModel();
    if (!staticModel) return false;

    // Cast 5 upward rays from the candidate position
    // If 4+ rays hit geometry overhead, classify as indoor
    int hits = 0;
    s_tl_collider.ray_options(CDB::OPT_ONLYNEAREST);

    Fvector upDirs[INDOOR_RAY_COUNT] = {
        {  0.0f, 1.0f,   0.0f },
        {  0.2f, 0.98f,  0.0f },
        { -0.2f, 0.98f,  0.0f },
        {  0.0f, 0.98f,  0.2f },
        {  0.0f, 0.98f, -0.2f }
    };

    for (int i = 0; i < INDOOR_RAY_COUNT; i++)
    {
        upDirs[i].normalize();
        s_tl_collider.ray_query(staticModel, pos, upDirs[i], INDOOR_RAY_RANGE);
        if (s_tl_collider.r_count() > 0)
            hits++;
    }

    return hits >= INDOOR_RAY_THRESHOLD;
}

CLightProbe CLightProbeGrid::MakeDefaultProbe(const Fvector& pos)
{
    CLightProbe probe;
    probe.position = pos;
    probe.skyVisibility = 0.0f;
    probe.ambient.set(0, 0, 0);
    probe.sunVisibility = 0.0f;
    probe.shDirection.set(0, 0, 0);
    probe._shPad = 0.0f;
    probe.pointLightColor.set(0, 0, 0);
    probe.pointLightIntensity = 0.0f;
    probe.envLuminance = 0.0f;
    probe.sectorId = 0xFFFF;  // Unused — placement is CFORM-based
    probe.lastUpdateFrame = 0;
    return probe;
}

void CLightProbeGrid::PlacePortalBridgeProbes(CPortal* portal)
{
    if (!portal) return;

    // Get portal center from bounding sphere and normal from plane
    Fvector center = portal->S.P;  // Sphere center
    Fvector normal = portal->P.n;  // Plane normal

    // Place probes on both sides of the portal
    Fvector pos1, pos2;
    pos1.mad(center, normal, PORTAL_BRIDGE_OFFSET);
    pos2.mad(center, normal, -PORTAL_BRIDGE_OFFSET);

    if (IsValidProbePosition(pos1))
        m_probes.push_back(MakeDefaultProbe(pos1));
    if (IsValidProbePosition(pos2))
        m_probes.push_back(MakeDefaultProbe(pos2));
}

bool CLightProbeGrid::IsValidProbePosition(const Fvector& pos)
{
    if (!g_pGameLevel) return false;

    CDB::MODEL* staticModel = g_pGameLevel->ObjectSpace.GetStaticModel();
    if (!staticModel) return false;

    // Check if position is inside geometry by casting ray down
    s_tl_collider.ray_options(CDB::OPT_ONLYNEAREST);
    s_tl_collider.ray_query(staticModel, pos, Fvector().set(0, -1, 0), 2.0f);

    // Valid if ray hit something below (not floating in air)
    if (s_tl_collider.r_count() == 0)
        return false;

    // Check if not inside solid geometry by casting short rays in 6 directions
    Fvector dirs[6] = {
        { 1, 0, 0 }, { -1, 0, 0 },
        { 0, 1, 0 }, { 0, -1, 0 },
        { 0, 0, 1 }, { 0, 0, -1 }
    };

    int blockedCount = 0;
    for (int i = 0; i < 6; i++)
    {
        s_tl_collider.ray_query(staticModel, pos, dirs[i], 0.3f);
        if (s_tl_collider.r_count() > 0)
            blockedCount++;
    }

    // Too many blocked directions = inside wall
    return blockedCount < 4;
}

void CLightProbeGrid::Build()
{
    if (ps_r3_ssfx_il == 0) return;

    Clear();

    Msg("* [LightProbeGrid] Building probe grid...");

    if (!g_pGameLevel) return;

    // Get CFORM level bounds — the actual geometric extent of the level
    const Fbox& levelBounds = g_pGameLevel->ObjectSpace.GetBoundingVolume();
    Fvector bMin = levelBounds.min;
    Fvector bMax = levelBounds.max;

    Msg("* [LightProbeGrid] CFORM bounds: (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)",
        bMin.x, bMin.y, bMin.z, bMax.x, bMax.y, bMax.z);

    // =====================================================================
    // Pass 1: Outdoor sweep at OUTDOOR_GRID_SPACING (2.0m)
    // Covers the entire level uniformly; counts indoor hits for diagnostics.
    // =====================================================================
    u32 outdoorCount = 0;
    u32 indoorTagged = 0;

    for (float x = bMin.x; x <= bMax.x; x += OUTDOOR_GRID_SPACING)
    for (float z = bMin.z; z <= bMax.z; z += OUTDOOR_GRID_SPACING)
    for (float y = bMin.y; y <= bMax.y; y += OUTDOOR_GRID_SPACING)
    {
        Fvector pos = { x, y, z };

        if (!IsValidProbePosition(pos))
            continue;

        bool isIndoor = IsPositionIndoor(pos);
        if (isIndoor) indoorTagged++;

        m_probes.push_back(MakeDefaultProbe(pos));
        outdoorCount++;
    }

    Msg("* [LightProbeGrid] Pass 1 (outdoor sweep): %d probes (%d tagged indoor)", outdoorCount, indoorTagged);

    // Build interim spatial hash — needed for HasNearbyProbe() in pass 2
    ComputeGridBounds();
    BuildSpatialHash();

    // =====================================================================
    // Pass 2: Indoor densification at INDOOR_GRID_SPACING (1.5m)
    // Fills gaps in indoor areas where the coarser outdoor grid misses detail.
    // Only places probes where no existing probe is within 80% of indoor spacing.
    // =====================================================================
    u32 indoorDensified = 0;
    float minSep = INDOOR_GRID_SPACING * 0.8f;

    for (float x = bMin.x; x <= bMax.x; x += INDOOR_GRID_SPACING)
    for (float z = bMin.z; z <= bMax.z; z += INDOOR_GRID_SPACING)
    for (float y = bMin.y; y <= bMax.y; y += INDOOR_GRID_SPACING)
    {
        Fvector pos = { x, y, z };

        // Skip if already covered by pass 1
        if (HasNearbyProbe(pos, minSep))
            continue;

        if (!IsValidProbePosition(pos))
            continue;

        // Only densify indoor areas
        if (!IsPositionIndoor(pos))
            continue;

        m_probes.push_back(MakeDefaultProbe(pos));
        indoorDensified++;
    }

    Msg("* [LightProbeGrid] Pass 2 (indoor densify): %d probes", indoorDensified);

    // =====================================================================
    // Portal bridge probes — placed at sector transitions for smooth lighting
    // =====================================================================
    for (u32 i = 0; i < RImplementation.Portals.size(); i++)
    {
        CPortal* portal = (CPortal*)RImplementation.Portals[i];
        if (!portal) continue;

        PlacePortalBridgeProbes(portal);
    }

    Msg("* [LightProbeGrid] Total probes after portal bridges: %d", m_probes.size());

    if (m_probes.empty())
    {
        Msg("* [LightProbeGrid] No probes placed - skipping build");
        return;
    }

    // Rebuild spatial hash with all probes (including pass 2 + portals)
    ComputeGridBounds();
    BuildSpatialHash();

    // Initial full update of all probes — runs in parallel via PPL.
    // Each thread uses its own thread_local CDB::COLLIDER and spatial query buffer,
    // so all per-probe computation is fully isolated.
    // bInitialBuild=true: skips UpdateVolumeProbe (RasterizeVolume() follows) and
    // the per-probe m_gpuBufferDirty write (set once below after the loop).
    Msg("* [LightProbeGrid] Performing initial probe update (parallel)...");
    xr_parallel_for(0u, (u32)m_probes.size(), [this](u32 i) {
        UpdateProbe(m_probes[i], i, PROBE_QUALITY_FULL, /*bInitialBuild=*/true);
    });
    m_gpuBufferDirty = true;

    // Initialize persistent GPU cache and frustum list
    InitGPUCache();
    m_frustumProbes.reserve(m_probes.size());

    m_gpuBufferDirty = true;
    PrepareGPUBuffer();

    // Build volume textures and perform initial rasterization
    BuildVolumeTextures();
    RasterizeVolume();
    UploadFullVolume();
    m_needFullUpload = false;  // Initial upload complete

    Msg("* [LightProbeGrid] Placed %d probes, bounds (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)",
        m_probes.size(),
        m_boundsMin.x, m_boundsMin.y, m_boundsMin.z,
        m_boundsMax.x, m_boundsMax.y, m_boundsMax.z);
    Msg("* [LightProbeGrid] Volume texture: %dx%dx%d voxels (%.1fm voxel size, %.2f MB)",
        m_volDims.x, m_volDims.y, m_volDims.z, m_voxelSize,
        (float)(m_volDims.x * m_volDims.y * m_volDims.z * 16 * NUM_VOLUME_TEXTURES) / (1024.0f * 1024.0f));
}

void CLightProbeGrid::ComputeGridBounds()
{
    if (m_probes.empty())
    {
        m_boundsMin.set(0, 0, 0);
        m_boundsMax.set(0, 0, 0);
        m_gridDims.set(1, 1, 1);
        return;
    }

    m_boundsMin = m_probes[0].position;
    m_boundsMax = m_probes[0].position;

    for (const auto& probe : m_probes)
    {
        m_boundsMin.min(probe.position);
        m_boundsMax.max(probe.position);
    }

    // Compute approximate grid dimensions
    Fvector extent;
    extent.sub(m_boundsMax, m_boundsMin);

    float avgSpacing = (OUTDOOR_GRID_SPACING + INDOOR_GRID_SPACING) / 2.0f;
    m_gridDims.x = _max(1, (int)(extent.x / avgSpacing) + 1);
    m_gridDims.y = _max(1, (int)(extent.y / avgSpacing) + 1);
    m_gridDims.z = _max(1, (int)(extent.z / avgSpacing) + 1);
}

float CLightProbeGrid::ComputeEnvLuminance() const
{
    if (!g_pGamePersistent || !g_pGamePersistent->Environment().CurrentEnv)
        return 1.0f;

    CEnvDescriptorMixer& env = *g_pGamePersistent->Environment().CurrentEnv;
    float hemiLum = env.hemi_color.x * 0.2126f + env.hemi_color.y * 0.7152f + env.hemi_color.z * 0.0722f;
    float sunLum  = env.sun_color.x  * 0.2126f + env.sun_color.y  * 0.7152f + env.sun_color.z  * 0.0722f;
    return hemiLum + sunLum * 0.5f;
}

//////////////////////////////////////////////////////////////////////////
// Volume Texture Implementation
//////////////////////////////////////////////////////////////////////////

ID3D11Texture3D* CLightProbeGrid::GetVolumeTexture(int idx) const
{
    if (idx >= 0 && idx < NUM_VOLUME_TEXTURES)
        return m_pVolTexture[idx];
    return nullptr;
}

ID3D11ShaderResourceView* CLightProbeGrid::GetVolumeSRV(int idx) const
{
    if (idx >= 0 && idx < NUM_VOLUME_TEXTURES)
        return m_pVolSRV[idx];
    return nullptr;
}

ID3D11UnorderedAccessView* CLightProbeGrid::GetVolumeUAV(int idx) const
{
    if (idx >= 0 && idx < NUM_VOLUME_TEXTURES)
        return m_pVolUAV[idx];
    return nullptr;
}

void CLightProbeGrid::BuildVolumeTextures()
{
    if (m_probes.empty()) return;

    // Compute volume bounds (same as grid bounds with margin)
    Fvector margin = { 1.0f, 1.0f, 1.0f };
    m_volMin.sub(m_boundsMin, margin);

    Fvector volMax;
    volMax.add(m_boundsMax, margin);
    m_volSize.sub(volMax, m_volMin);

    // Compute voxel size — start at OUTDOOR_GRID_SPACING, auto-coarsen if too many voxels
    m_voxelSize = OUTDOOR_GRID_SPACING;
    int volX, volY, volZ;

    for (;;)
    {
        volX = _max(1, (int)ceilf(m_volSize.x / m_voxelSize));
        volY = _max(1, (int)ceilf(m_volSize.y / m_voxelSize));
        volZ = _max(1, (int)ceilf(m_volSize.z / m_voxelSize));

        if ((u32)(volX * volY * volZ) <= MAX_VOLUME_VOXELS || m_voxelSize >= 8.0f)
            break;

        m_voxelSize += 0.5f;
    }

    m_volDims.set(volX, volY, volZ);

    // Recompute exact volume size to match voxel grid
    m_volSize.set(volX * m_voxelSize, volY * m_voxelSize, volZ * m_voxelSize);

    u32 totalVoxels = volX * volY * volZ;

    Msg("* [LightProbeGrid] Building volume textures: %dx%dx%d = %d voxels (%.1fm voxel size)",
        volX, volY, volZ, totalVoxels, m_voxelSize);

    // Create 3 × Texture3D with D3D11_USAGE_DEFAULT + UAV
    // DEFAULT usage: persistent GPU memory, no per-frame reallocation.
    // Sparse updates via compute shader (structured buffer → UAV scatter).
    // Full uploads via UpdateSubresource (Build/time jump only).
    for (int i = 0; i < NUM_VOLUME_TEXTURES; i++)
    {
        if (m_pVolUAV[i]) { m_pVolUAV[i]->Release(); m_pVolUAV[i] = nullptr; }
        if (m_pVolSRV[i]) { m_pVolSRV[i]->Release(); m_pVolSRV[i] = nullptr; }
        if (m_pVolTexture[i]) { m_pVolTexture[i]->Release(); m_pVolTexture[i] = nullptr; }

        D3D11_TEXTURE3D_DESC texDesc = {};
        texDesc.Width = volX;
        texDesc.Height = volY;
        texDesc.Depth = volZ;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        texDesc.CPUAccessFlags = 0;

        HRESULT hr = HW.pDevice->CreateTexture3D(&texDesc, nullptr, &m_pVolTexture[i]);
        if (FAILED(hr))
        {
            Msg("! [LightProbeGrid] CreateTexture3D[%d] failed: 0x%08X (%dx%dx%d)", i, hr, volX, volY, volZ);
            return;
        }

        // Create SRV (auto-detects Texture3D dimension)
        hr = HW.pDevice->CreateShaderResourceView(m_pVolTexture[i], nullptr, &m_pVolSRV[i]);
        if (FAILED(hr))
        {
            Msg("! [LightProbeGrid] CreateSRV[%d] failed: 0x%08X", i, hr);
            return;
        }

        // Create UAV for compute shader scatter writes
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.MipSlice = 0;
        uavDesc.Texture3D.FirstWSlice = 0;
        uavDesc.Texture3D.WSize = volZ;

        hr = HW.pDevice->CreateUnorderedAccessView(m_pVolTexture[i], &uavDesc, &m_pVolUAV[i]);
        if (FAILED(hr))
        {
            Msg("! [LightProbeGrid] CreateUAV[%d] failed: 0x%08X", i, hr);
            return;
        }
    }

    // Create structured buffer for sparse voxel updates
    // CPU fills via MAP_WRITE_DISCARD (tiny: ~64KB normal frame vs 24MB full volume)
    {
        if (m_pUpdateSRV) { m_pUpdateSRV->Release(); m_pUpdateSRV = nullptr; }
        if (m_pUpdateBuffer) { m_pUpdateBuffer->Release(); m_pUpdateBuffer = nullptr; }

        D3D11_BUFFER_DESC bufDesc = {};
        bufDesc.ByteWidth = MAX_SPARSE_VOXEL_UPDATES * sizeof(VoxelGPUUpdate);
        bufDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufDesc.StructureByteStride = sizeof(VoxelGPUUpdate);

        HRESULT hr = HW.pDevice->CreateBuffer(&bufDesc, nullptr, &m_pUpdateBuffer);
        if (FAILED(hr))
        {
            Msg("! [LightProbeGrid] CreateBuffer (structured) failed: 0x%08X", hr);
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;  // Required for structured buffers
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = MAX_SPARSE_VOXEL_UPDATES;

        hr = HW.pDevice->CreateShaderResourceView(m_pUpdateBuffer, &srvDesc, &m_pUpdateSRV);
        if (FAILED(hr))
        {
            Msg("! [LightProbeGrid] CreateSRV (structured) failed: 0x%08X", hr);
            return;
        }
    }

    // Allocate accumulator, staging buffers, and dirty tracking
    m_volAccum.resize(totalVoxels);
    for (int i = 0; i < NUM_VOLUME_TEXTURES; i++)
        m_volData[i].resize(totalVoxels * 4);  // 4 floats per voxel
    m_voxelDirtyFlags.resize(totalVoxels, 0);
    m_dirtyVoxelCount = 0;
    m_pendingUpdateCount = 0;

    m_needFullUpload = true;  // First upload after build is always full
}

void CLightProbeGrid::RasterizeVolume()
{
    if (m_volAccum.empty() || m_probes.empty()) return;

    u32 totalVoxels = m_volDims.x * m_volDims.y * m_volDims.z;

    // Phase 1: Zero accumulators
    memset(m_volAccum.data(), 0, totalVoxels * sizeof(VoxelAccum));

    float sigma = m_voxelSize * 0.85f;  // Sharper scatter preserves indoor gradients
    float invSigmaSq2 = -0.5f / (sigma * sigma);

    // Phase 2: Scatter — for each probe, distribute to 3×3×3 voxel neighborhood
    for (u32 pi = 0; pi < m_probes.size(); pi++)
    {
        const CLightProbe& probe = m_probes[pi];

        // Find center voxel
        Fvector local;
        local.sub(probe.position, m_volMin);
        int cx = (int)(local.x / m_voxelSize);
        int cy = (int)(local.y / m_voxelSize);
        int cz = (int)(local.z / m_voxelSize);

        // Scatter to 3×3×3 neighborhood
        for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
            int vx = cx + dx;
            int vy = cy + dy;
            int vz = cz + dz;

            if (vx < 0 || vx >= m_volDims.x) continue;
            if (vy < 0 || vy >= m_volDims.y) continue;
            if (vz < 0 || vz >= m_volDims.z) continue;

            // Compute voxel center in world space
            Fvector voxelCenter;
            voxelCenter.x = m_volMin.x + (vx + 0.5f) * m_voxelSize;
            voxelCenter.y = m_volMin.y + (vy + 0.5f) * m_voxelSize;
            voxelCenter.z = m_volMin.z + (vz + 0.5f) * m_voxelSize;

            float distSq = probe.position.distance_to_sqr(voxelCenter);
            float w = expf(distSq * invSigmaSq2);

            int voxelIdx = vz * (m_volDims.x * m_volDims.y) + vy * m_volDims.x + vx;
            VoxelAccum& acc = m_volAccum[voxelIdx];

            acc.ambient[0]     += probe.ambient.x * w;
            acc.ambient[1]     += probe.ambient.y * w;
            acc.ambient[2]     += probe.ambient.z * w;
            acc.skyVis         += probe.skyVisibility * w;
            acc.sunVis         += probe.sunVisibility * w;
            acc.shDir[0] += probe.shDirection.x * w;
            acc.shDir[1] += probe.shDirection.y * w;
            acc.shDir[2] += probe.shDirection.z * w;
            acc.pointLight[0]  += probe.pointLightColor.x * w;
            acc.pointLight[1]  += probe.pointLightColor.y * w;
            acc.pointLight[2]  += probe.pointLightColor.z * w;
            acc.weight         += w;
        }
    }

    // Phase 3: Normalize and write to staging buffers
    for (u32 v = 0; v < totalVoxels; v++)
    {
        const VoxelAccum& acc = m_volAccum[v];

        float invW = (acc.weight > 0.0001f) ? (1.0f / acc.weight) : 0.0f;

        // vol0: ambient.rgb, skyVisibility
        m_volData[0][v * 4 + 0] = acc.ambient[0] * invW;
        m_volData[0][v * 4 + 1] = acc.ambient[1] * invW;
        m_volData[0][v * 4 + 2] = acc.ambient[2] * invW;
        m_volData[0][v * 4 + 3] = acc.skyVis * invW;

        // vol1: shDirection.xyz, 0.0
        m_volData[1][v * 4 + 0] = acc.shDir[0] * invW;
        m_volData[1][v * 4 + 1] = acc.shDir[1] * invW;
        m_volData[1][v * 4 + 2] = acc.shDir[2] * invW;
        m_volData[1][v * 4 + 3] = 0.0f;

        // vol2: pointLightColor.rgb, sunVisibility
        m_volData[2][v * 4 + 0] = acc.pointLight[0] * invW;
        m_volData[2][v * 4 + 1] = acc.pointLight[1] * invW;
        m_volData[2][v * 4 + 2] = acc.pointLight[2] * invW;
        m_volData[2][v * 4 + 3] = acc.sunVis * invW;
    }

    // Phase 4: Column-wise flood-fill for empty voxels
    // Probes exist only at terrain surface level, leaving empty voxels above (sky)
    // and below (underground) with weight=0 → all values=0. Without fill,
    // GPU trilinear interpolation between populated (e.g. skyVis=1.0) and empty
    // (skyVis=0.0) voxels produces incorrect intermediate values (0.5).
    // Fix: propagate nearest populated voxel's data vertically through each column.
    int sliceStride = m_volDims.x * m_volDims.y;

    for (int vz = 0; vz < m_volDims.z; vz++)
    {
        for (int vx = 0; vx < m_volDims.x; vx++)
        {
            // Pass 1: Upward sweep — fills empty voxels above each populated voxel
            int lastPopIdx = -1;
            for (int vy = 0; vy < m_volDims.y; vy++)
            {
                int idx = vz * sliceStride + vy * m_volDims.x + vx;
                if (m_volAccum[idx].weight > 0.0001f)
                    lastPopIdx = idx;
                else if (lastPopIdx >= 0)
                {
                    for (int vol = 0; vol < NUM_VOLUME_TEXTURES; vol++)
                        memcpy(&m_volData[vol][idx * 4], &m_volData[vol][lastPopIdx * 4], 4 * sizeof(float));
                }
            }

            // Pass 2: Find lowest populated voxel and fill everything below it
            for (int vy = 0; vy < m_volDims.y; vy++)
            {
                int idx = vz * sliceStride + vy * m_volDims.x + vx;
                if (m_volAccum[idx].weight > 0.0001f)
                {
                    // Fill all voxels below this one
                    for (int by = vy - 1; by >= 0; by--)
                    {
                        int belowIdx = vz * sliceStride + by * m_volDims.x + vx;
                        for (int vol = 0; vol < NUM_VOLUME_TEXTURES; vol++)
                            memcpy(&m_volData[vol][belowIdx * 4], &m_volData[vol][idx * 4], 4 * sizeof(float));
                    }
                    break;  // Only need the lowest populated voxel
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// NormalizeVoxel — re-normalize one voxel from accum → staging + mark dirty
//////////////////////////////////////////////////////////////////////////
void CLightProbeGrid::NormalizeVoxel(int voxelIdx)
{
    const VoxelAccum& acc = m_volAccum[voxelIdx];
    float invW = (acc.weight > 0.0001f) ? (1.0f / acc.weight) : 0.0f;

    m_volData[0][voxelIdx * 4 + 0] = acc.ambient[0] * invW;
    m_volData[0][voxelIdx * 4 + 1] = acc.ambient[1] * invW;
    m_volData[0][voxelIdx * 4 + 2] = acc.ambient[2] * invW;
    m_volData[0][voxelIdx * 4 + 3] = acc.skyVis * invW;

    m_volData[1][voxelIdx * 4 + 0] = acc.shDir[0] * invW;
    m_volData[1][voxelIdx * 4 + 1] = acc.shDir[1] * invW;
    m_volData[1][voxelIdx * 4 + 2] = acc.shDir[2] * invW;
    m_volData[1][voxelIdx * 4 + 3] = 0.0f;

    m_volData[2][voxelIdx * 4 + 0] = acc.pointLight[0] * invW;
    m_volData[2][voxelIdx * 4 + 1] = acc.pointLight[1] * invW;
    m_volData[2][voxelIdx * 4 + 2] = acc.pointLight[2] * invW;
    m_volData[2][voxelIdx * 4 + 3] = acc.sunVis * invW;

    // Track dirty voxel for sparse GPU update (deduplicates via flag)
    if (!m_voxelDirtyFlags.empty() && !m_voxelDirtyFlags[voxelIdx])
    {
        m_voxelDirtyFlags[voxelIdx] = 1;
        m_dirtyVoxelCount++;
    }
}

//////////////////////////////////////////////////////////////////////////
// UpdateVolumeProbe — incremental update: subtract old, add new for 27 voxels
// Avoids full rasterization by only touching the voxels this probe affects.
// Gaussian weights depend only on position (probes are static), so they're
// the same for both old and new contributions.
//////////////////////////////////////////////////////////////////////////
void CLightProbeGrid::UpdateVolumeProbe(u32 probeIndex, const CLightProbe& oldValues)
{
    if (m_volAccum.empty() || m_volDims.x == 0) return;

    const CLightProbe& newProbe = m_probes[probeIndex];

    // Find center voxel (same as in RasterizeVolume)
    Fvector local;
    local.sub(newProbe.position, m_volMin);
    int cx = (int)(local.x / m_voxelSize);
    int cy = (int)(local.y / m_voxelSize);
    int cz = (int)(local.z / m_voxelSize);

    float sigma = m_voxelSize * 0.85f;  // Sharper scatter preserves indoor gradients
    float invSigmaSq2 = -0.5f / (sigma * sigma);

    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        int vx = cx + dx;
        int vy = cy + dy;
        int vz = cz + dz;

        if (vx < 0 || vx >= m_volDims.x) continue;
        if (vy < 0 || vy >= m_volDims.y) continue;
        if (vz < 0 || vz >= m_volDims.z) continue;

        Fvector voxelCenter;
        voxelCenter.x = m_volMin.x + (vx + 0.5f) * m_voxelSize;
        voxelCenter.y = m_volMin.y + (vy + 0.5f) * m_voxelSize;
        voxelCenter.z = m_volMin.z + (vz + 0.5f) * m_voxelSize;

        float distSq = newProbe.position.distance_to_sqr(voxelCenter);
        float w = expf(distSq * invSigmaSq2);

        int voxelIdx = vz * (m_volDims.x * m_volDims.y) + vy * m_volDims.x + vx;
        VoxelAccum& acc = m_volAccum[voxelIdx];

        // Subtract old contribution
        acc.ambient[0]     -= oldValues.ambient.x * w;
        acc.ambient[1]     -= oldValues.ambient.y * w;
        acc.ambient[2]     -= oldValues.ambient.z * w;
        acc.skyVis         -= oldValues.skyVisibility * w;
        acc.sunVis         -= oldValues.sunVisibility * w;
        acc.shDir[0]       -= oldValues.shDirection.x * w;
        acc.shDir[1]       -= oldValues.shDirection.y * w;
        acc.shDir[2]       -= oldValues.shDirection.z * w;
        acc.pointLight[0]  -= oldValues.pointLightColor.x * w;
        acc.pointLight[1]  -= oldValues.pointLightColor.y * w;
        acc.pointLight[2]  -= oldValues.pointLightColor.z * w;

        // Add new contribution
        acc.ambient[0]     += newProbe.ambient.x * w;
        acc.ambient[1]     += newProbe.ambient.y * w;
        acc.ambient[2]     += newProbe.ambient.z * w;
        acc.skyVis         += newProbe.skyVisibility * w;
        acc.sunVis         += newProbe.sunVisibility * w;
        acc.shDir[0]       += newProbe.shDirection.x * w;
        acc.shDir[1]       += newProbe.shDirection.y * w;
        acc.shDir[2]       += newProbe.shDirection.z * w;
        acc.pointLight[0]  += newProbe.pointLightColor.x * w;
        acc.pointLight[1]  += newProbe.pointLightColor.y * w;
        acc.pointLight[2]  += newProbe.pointLightColor.z * w;
        // Note: weight unchanged — same probe, same position, same w

        NormalizeVoxel(voxelIdx);
    }

    m_volDirty = true;  // Mark for GPU upload (cheap memcpy only, no rasterization)
}

//////////////////////////////////////////////////////////////////////////
// UploadFullVolume — UpdateSubresource for bulk uploads (Build/time jump/overflow)
// Used when too many voxels changed for sparse path or after full RasterizeVolume().
//////////////////////////////////////////////////////////////////////////
void CLightProbeGrid::UploadFullVolume()
{
    if (m_volDims.x == 0 || m_volDims.y == 0 || m_volDims.z == 0) return;

    u32 rowPitch   = m_volDims.x * 4 * sizeof(float);  // 16 bytes per voxel
    u32 depthPitch = rowPitch * m_volDims.y;

    for (int i = 0; i < NUM_VOLUME_TEXTURES; i++)
    {
        if (!m_pVolTexture[i] || m_volData[i].empty()) continue;
        HW.pContext->UpdateSubresource(m_pVolTexture[i], 0, nullptr,
            m_volData[i].data(), rowPitch, depthPitch);
    }
}

//////////////////////////////////////////////////////////////////////////
// PrepareVolumeUpdate — fill structured buffer from dirty flags
// CPU iterates dirty flag bitset, packs voxel data into VoxelGPUUpdate entries,
// uploads via MAP_WRITE_DISCARD on the structured buffer (~64KB typical).
// CRenderTarget::phase_probe_volume_update() dispatches the compute shader later.
//////////////////////////////////////////////////////////////////////////
void CLightProbeGrid::PrepareVolumeUpdate()
{
    if (!m_pUpdateBuffer || m_dirtyVoxelCount == 0) return;

    u32 totalVoxels = m_volDims.x * m_volDims.y * m_volDims.z;
    int sliceStride = m_volDims.x * m_volDims.y;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(HW.pContext->Map(m_pUpdateBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;

    VoxelGPUUpdate* updates = (VoxelGPUUpdate*)mapped.pData;
    u32 count = 0;

    for (u32 v = 0; v < totalVoxels && count < MAX_SPARSE_VOXEL_UPDATES; v++)
    {
        if (!m_voxelDirtyFlags[v]) continue;

        VoxelGPUUpdate& u = updates[count];

        // Convert flat index to 3D coordinates
        int z = v / sliceStride;
        int remainder = v - z * sliceStride;
        int y = remainder / m_volDims.x;
        int x = remainder - y * m_volDims.x;

        u.x = (u32)x;
        u.y = (u32)y;
        u.z = (u32)z;
        u._pad0 = 0;

        // Pack staging buffer data
        memcpy(u.vol0, &m_volData[0][v * 4], 4 * sizeof(float));
        memcpy(u.vol1, &m_volData[1][v * 4], 4 * sizeof(float));
        memcpy(u.vol2, &m_volData[2][v * 4], 4 * sizeof(float));

        count++;
    }

    HW.pContext->Unmap(m_pUpdateBuffer, 0);

    m_pendingUpdateCount = count;

    // Clear dirty tracking
    memset(m_voxelDirtyFlags.data(), 0, totalVoxels);
    m_dirtyVoxelCount = 0;
    m_volDirty = false;
}

//////////////////////////////////////////////////////////////////////////
// Simplified 2-Tier Scheduling
//////////////////////////////////////////////////////////////////////////

void CLightProbeGrid::RebuildFrustumList()
{
    m_frustumProbes.clear();

    for (u32 i = 0; i < m_probes.size(); i++)
    {
        if (m_viewFrustum.testSphere_dirty(m_probes[i].position, 1.0f))
            m_frustumProbes.push_back(i);
    }
}

void CLightProbeGrid::UpdateProbe(CLightProbe& probe, u32 probeIndex, EProbeQuality quality, bool bInitialBuild)
{
    // Snapshot old values BEFORE modification for incremental volume update
    CLightProbe oldValues = probe;

    probe.lastUpdateFrame = (u16)(m_currentFrame & 0xFFFF);

    float skyHits = 0;
    float totalRays = 0;
    Fvector ambientAccum = { 0, 0, 0 };

    // Get current environment data
    if (!g_pGamePersistent || !g_pGamePersistent->Environment().CurrentEnv)
        return;

    CEnvDescriptorMixer& env = *g_pGamePersistent->Environment().CurrentEnv;
    Fvector skyColor;
    skyColor.set(env.hemi_color.x, env.hemi_color.y, env.hemi_color.z);
    Fvector sunDir;
    sunDir.set(env.sun_dir.x, env.sun_dir.y, env.sun_dir.z);
    sunDir.normalize_safe();
    sunDir.invert();  // env.sun_dir points FROM sun TO ground; we need toward-sun direction

    CDB::MODEL* staticModel = g_pGameLevel ? g_pGameLevel->ObjectSpace.GetStaticModel() : nullptr;
    if (!staticModel) return;

    s_tl_collider.ray_options(CDB::OPT_ONLYNEAREST);

    // Quality-dependent ray counts
    int hemisphereRayCount = (quality == PROBE_QUALITY_FULL) ? RAYS_PER_PROBE : 6;
    int shadowRayCount     = (quality == PROBE_QUALITY_FULL) ? SOFT_SHADOW_RAYS : 3;

    // Temporal rotation: golden-ratio azimuthal offset rotates the entire ray pattern
    // each update. Since probes update round-robin, m_currentFrame gives both temporal
    // diversity (same probe samples different dirs over time) and spatial diversity
    // (nearby probes sample different dirs on the same frame).
    float rotation = float(m_currentFrame) * s_invGoldenRatio;

    // Track received sunlight from hemisphere samples
    float receivedSunlight = 0;

    // Hemisphere rays — Fibonacci spiral with temporal rotation
    for (int i = 0; i < hemisphereRayCount; i++)
    {
        // Compute rotated Fibonacci hemisphere ray on-the-fly
        float y = 1.0f - (float(i) / float(hemisphereRayCount - 1));
        y = y * 0.9f + 0.1f;  // Avoid pure horizontal
        float radius_h = sqrtf(1.0f - y * y);
        float theta = 2.0f * PI * (float(i) / s_goldenRatio + rotation);

        Fvector dir;
        dir.x = cosf(theta) * radius_h;
        dir.y = y;
        dir.z = sinf(theta) * radius_h;
        dir.normalize();

        s_tl_collider.ray_query(staticModel, probe.position, dir, RAY_MAX_DISTANCE);
        totalRays += 1.0f;

        if (s_tl_collider.r_count() == 0)
        {
            // Ray escaped to sky
            skyHits += 1.0f;
            ambientAccum.add(skyColor);

            // Check if this sky ray is toward the sun (receiving direct sunlight through opening)
            float sunAlignment = dir.dotproduct(sunDir);
            if (sunAlignment > SUN_ALIGNMENT_THRESHOLD)
            {
                receivedSunlight += sunAlignment;
            }
        }
    }

    // Normalize received sunlight by actual ray count used
    receivedSunlight = _min(receivedSunlight / hemisphereRayCount, 1.0f);

    // Soft shadow sun visibility - cast jittered rays toward sun
    float directSunVis = 0;

    // Build tangent frame for jittering around sun direction
    Fvector sunTangent, sunBitangent;
    if (fabsf(sunDir.y) < 0.99f)
    {
        sunTangent.crossproduct(sunDir, Fvector().set(0, 1, 0));
    }
    else
    {
        sunTangent.crossproduct(sunDir, Fvector().set(1, 0, 0));
    }
    sunTangent.normalize();
    sunBitangent.crossproduct(sunDir, sunTangent);
    sunBitangent.normalize();

    // Cast jittered rays for soft shadows (with temporal rotation)
    for (int i = 0; i < shadowRayCount; i++)
    {
        // Deterministic jitter pattern (Fibonacci-like spiral) + temporal rotation
        float angle = (float)i * 2.399f + rotation * 2.0f * PI;  // Golden angle + temporal offset
        float radius = SOFT_SHADOW_JITTER * (0.3f + 0.7f * (float)i / (float)shadowRayCount);

        Fvector jitteredDir;
        jitteredDir.set(sunDir);
        jitteredDir.mad(sunTangent, cosf(angle) * radius);
        jitteredDir.mad(sunBitangent, sinf(angle) * radius);
        jitteredDir.normalize();

        s_tl_collider.ray_query(staticModel, probe.position, jitteredDir, RAY_MAX_DISTANCE);
        if (s_tl_collider.r_count() == 0)
        {
            directSunVis += 1.0f / shadowRayCount;
        }
    }

    // Combine direct sun visibility with received sunlight, then temporal smooth.
    // With temporal rotation, each frame samples a different jitter pattern, so
    // temporal smoothing now integrates many more shadow directions over time.
    float combinedSunVis = _max(directSunVis, receivedSunlight * RECEIVED_LIGHT_WEIGHT);

    // =========================================================================
    // Adaptive temporal smoothing
    // Normal: 70% old / 30% new. Time jump: up to 100% new (instant snap).
    // m_temporalBlend is set per-frame in Update() based on game time delta.
    // =========================================================================
    float blend = m_temporalBlend;
    float keep  = 1.0f - blend;

    float newSkyVis = totalRays > 0 ? (skyHits / totalRays) : 0.0f;
    probe.skyVisibility = probe.skyVisibility * keep + newSkyVis * blend;

    // Sun visibility: temporal smooth (was previously set directly, now benefits
    // from temporal rotation — each frame samples different jitter directions)
    probe.sunVisibility = probe.sunVisibility * keep + combinedSunVis * blend;

    Fvector newAmbient;
    if (totalRays > 0)
        newAmbient.set(ambientAccum).div(totalRays);
    else
        newAmbient.set(0, 0, 0);

    probe.ambient.lerp(probe.ambient, newAmbient, blend);

    // Store environment luminance for ToD snap (used when this probe goes stale)
    probe.envLuminance = m_currentEnvLum;

    // During initial build (bInitialBuild=true):
    //   - WriteProbeToCache is a no-op anyway (m_gpuCache is empty before InitGPUCache())
    //   - m_gpuBufferDirty is set once outside the parallel loop
    //   - UpdateVolumeProbe is skipped entirely: RasterizeVolume() follows the loop and rebuilds
    //     the volume from scratch, so per-probe incremental updates here are pure wasted work.
    if (!bInitialBuild)
    {
        WriteProbeToCache(probeIndex);
        m_gpuBufferDirty = true;
        // Incremental volume update: only touches 27 voxels instead of full rasterization
        UpdateVolumeProbe(probeIndex, oldValues);
    }
}

void CLightProbeGrid::Update()
{
    if (m_probes.empty()) return;

    m_currentFrame++;
    m_updateBudget = (u32)ps_r_probe_update_rate;
    m_debugEnabled = ps_r_debug_probes != 0;

    CTimer updateTimer;
    updateTimer.Start();

    Fvector playerPos = Device.vCameraPosition;

    // =========================================================================
    // Time-of-day jump detection + adaptive temporal smoothing
    // =========================================================================
    // Default blend: 70% old / 30% new. During boost period (time jump),
    // maintain aggressive blend so ALL probes converge fast, not just those
    // updated on the single jump frame.
    m_temporalBlend = (m_budgetBoostFramesLeft > 0) ? 0.7f : 0.3f;
    m_currentEnvLum = ComputeEnvLuminance();

    if (g_pGamePersistent)
    {
        // GetVisualTime() returns editor_sun_time if weather editor is active,
        // otherwise fGameTime. This ensures the probe system detects weather
        // editor slider changes as time jumps (editor directly sets sun time
        // without affecting fGameTime).
        float currentGameTime = g_pGamePersistent->Environment().GetVisualTime();

        if (m_lastGameTime >= 0.0f)
        {
            float timeDelta = fabsf(currentGameTime - m_lastGameTime);
            // Handle midnight wrap-around (86400 seconds/day)
            if (timeDelta > 43200.0f)
                timeDelta = 86400.0f - timeDelta;

            if (timeDelta > 3600.0f)
            {
                // Large jump (>1 game hour): env snap all probes + invalidate for re-ray
                if (m_currentEnvLum > 0.001f)
                {
                    for (u32 i = 0; i < m_probes.size(); i++)
                    {
                        CLightProbe& probe = m_probes[i];
                        if (probe.envLuminance > 0.001f)
                        {
                            float scale = _min(m_currentEnvLum / probe.envLuminance, 4.0f);
                            probe.ambient.mul(scale);
                            probe.envLuminance = m_currentEnvLum;
                            WriteProbeToCache(i);
                        }
                        probe.lastUpdateFrame = 0;
                    }
                    m_gpuBufferDirty = true;
                    // Full rasterization needed — all probes changed at once
                    RasterizeVolume();
                    m_needFullUpload = true;  // Trigger full UpdateSubresource
                }
                m_temporalBlend = 1.0f;  // Instant snap for subsequent ray updates
                m_budgetBoostFramesLeft = 120;  // 4× budget for ~2 seconds
            }
            else if (timeDelta > 60.0f)
            {
                // Medium jump (>1 game minute): aggressive blend
                m_temporalBlend = 0.7f;
                m_budgetBoostFramesLeft = 120;  // 4× budget for ~2 seconds
            }
        }
        m_lastGameTime = currentGameTime;

        // High time_factor detection — when players accelerate time via console
        // (e.g. time_factor 100), per-frame deltas are small (~1.67s) and never
        // hit the 60s jump threshold. Scale temporal blend proportionally so
        // probes converge faster during time-lapse.
        float timeFactor = g_pGamePersistent->Environment().fTimeFactor;
        if (timeFactor > 20.0f)  // Normal is 12; >20 = significantly accelerated
        {
            float accelRatio = _min((timeFactor - 20.0f) / 80.0f, 1.0f);  // 0→1 over 20→100
            m_temporalBlend = _max(m_temporalBlend, 0.3f + accelRatio * 0.4f);  // 0.3→0.7
        }
    }

    // =========================================================================
    // Build view frustum for frustum-prioritized scheduling
    // =========================================================================
    m_viewFrustum.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

    // =========================================================================
    // Camera rotation detection — boost budget on fast turns
    // =========================================================================
    float cameraDirDot = m_prevCameraDir.dotproduct(Device.vCameraDirection);
    bool cameraRotated = (m_currentFrame > 1) && (cameraDirDot < 0.95f);
    m_prevCameraDir = Device.vCameraDirection;

    // =========================================================================
    // 2-Tier scheduling: In-frustum + Background
    // =========================================================================
    // Compute budget with optional boost
    u32 budget = m_updateBudget;
    if (m_budgetBoostFramesLeft > 0)
    {
        budget *= 4;
        m_budgetBoostFramesLeft--;
    }

    // Camera rotation boost: bump in-frustum budget to 95% for 3 frames
    u32 frustumPct = cameraRotated ? 95 : 90;
    u32 frustumBudget = (budget * frustumPct) / 100;
    u32 bgBudget = budget - frustumBudget;

    // Rebuild frustum probe list every frame
    RebuildFrustumList();

    // =========================================================================
    // In-frustum tier: round-robin through m_frustumProbes
    // FULL quality for probes < 15m, REDUCED for > 15m
    // =========================================================================
    if (!m_frustumProbes.empty())
    {
        u32 frustumCount = (u32)m_frustumProbes.size();
        u32 updated = 0;

        for (u32 i = 0; i < frustumCount && updated < frustumBudget; i++)
        {
            u32 idx = m_frustumProbes[(m_frustumRobinIndex + i) % frustumCount];
            if (idx >= m_probes.size()) continue;

            // Skip if already updated this frame
            if (m_probes[idx].lastUpdateFrame == (u16)(m_currentFrame & 0xFFFF))
                continue;

            float distSq = playerPos.distance_to_sqr(m_probes[idx].position);
            EProbeQuality quality = (distSq < 15.0f * 15.0f) ? PROBE_QUALITY_FULL : PROBE_QUALITY_REDUCED;

            UpdateProbe(m_probes[idx], idx, quality);
            updated++;
        }
        m_frustumRobinIndex = (m_frustumRobinIndex + frustumBudget) % _max(1u, frustumCount);
    }

    // =========================================================================
    // Background tier: round-robin through ALL probes, REDUCED quality
    // =========================================================================
    {
        u32 probeCount = (u32)m_probes.size();
        u32 updated = 0;

        for (u32 i = 0; i < probeCount && updated < bgBudget; i++)
        {
            u32 idx = (m_bgRobinIndex + i) % probeCount;

            // Skip if already updated this frame
            if (m_probes[idx].lastUpdateFrame == (u16)(m_currentFrame & 0xFFFF))
                continue;

            UpdateProbe(m_probes[idx], idx, PROBE_QUALITY_REDUCED);
            updated++;
        }
        m_bgRobinIndex = (m_bgRobinIndex + bgBudget) % _max(1u, probeCount);
    }

    m_lastUpdateTimeMs = updateTimer.GetElapsed_sec() * 1000.0f;

    // =========================================================================
    // GPU upload — sparse compute dispatch or full UpdateSubresource
    // =========================================================================
    u32 uploadInterval = (u32)_max(1, ps_r_probe_upload_rate);
    bool shouldUpload = (m_currentFrame - m_lastUploadFrame >= uploadInterval);

    if (m_needFullUpload && shouldUpload)
    {
        // Full rasterization just happened (Build/time jump) — upload entire volume
        UploadFullVolume();
        m_needFullUpload = false;
        m_dirtyVoxelCount = 0;
        m_pendingUpdateCount = 0;
        if (!m_voxelDirtyFlags.empty())
            memset(m_voxelDirtyFlags.data(), 0, m_voxelDirtyFlags.size());
        m_volDirty = false;
        m_lastUploadFrame = m_currentFrame;
    }
    else if (m_volDirty && shouldUpload)
    {
        if (m_dirtyVoxelCount > MAX_SPARSE_VOXEL_UPDATES)
        {
            // Too many dirty voxels for sparse path — fall back to full upload
            UploadFullVolume();
            m_dirtyVoxelCount = 0;
            m_pendingUpdateCount = 0;
            if (!m_voxelDirtyFlags.empty())
                memset(m_voxelDirtyFlags.data(), 0, m_voxelDirtyFlags.size());
            m_volDirty = false;
        }
        else
        {
            // Sparse path: fill structured buffer for compute dispatch
            // Actual GPU dispatch happens in CRenderTarget::phase_probe_volume_update()
            PrepareVolumeUpdate();
        }
        m_lastUploadFrame = m_currentFrame;
    }

    // Probe data texture upload — only needed for debug visualization
    if (m_gpuBufferDirty && shouldUpload && ps_r_debug_probes != 0)
    {
        PrepareGPUBuffer();
    }
}

//////////////////////////////////////////////////////////////////////////
// GPU Cache — persistent CPU-side texel buffer
//////////////////////////////////////////////////////////////////////////

void CLightProbeGrid::WriteProbeToCache(u32 probeIndex)
{
    if (m_gpuCache.empty() || probeIndex >= m_probes.size()) return;

    const CLightProbe& probe = m_probes[probeIndex];
    u32 row = probeIndex / PROBES_PER_ROW;
    u32 col = (probeIndex % PROBES_PER_ROW) * 4;
    const u32 texelSize = 4 * sizeof(float);  // 16 bytes per RGBA32F texel

    float* texels = (float*)(m_gpuCache.data() + row * m_gpuCacheRowPitch + col * texelSize);

    // Texel 0: position.xyz, skyVisibility
    texels[0] = probe.position.x;
    texels[1] = probe.position.y;
    texels[2] = probe.position.z;
    texels[3] = probe.skyVisibility;

    // Texel 1: ambient.xyz, sunVisibility
    texels[4] = probe.ambient.x;
    texels[5] = probe.ambient.y;
    texels[6] = probe.ambient.z;
    texels[7] = probe.sunVisibility;

    // Texel 2: shDirection.xyz, 0.0 (L1 SH directional vector)
    texels[8]  = probe.shDirection.x;
    texels[9]  = probe.shDirection.y;
    texels[10] = probe.shDirection.z;
    texels[11] = 0.0f;

    // Texel 3: pointLightColor.xyz, pointLightIntensity
    texels[12] = probe.pointLightColor.x;
    texels[13] = probe.pointLightColor.y;
    texels[14] = probe.pointLightColor.z;
    texels[15] = probe.pointLightIntensity;
}

void CLightProbeGrid::InitGPUCache()
{
    if (m_probes.empty()) return;

    u32 probeCount = (u32)m_probes.size();
    u32 texWidth = PROBES_PER_ROW * 4;  // texels wide
    u32 texHeight = (probeCount + PROBES_PER_ROW - 1) / PROBES_PER_ROW;
    const u32 texelSize = 4 * sizeof(float);  // 16 bytes

    m_gpuCacheRowPitch = texWidth * texelSize;
    m_gpuCacheTexHeight = texHeight;
    m_gpuCache.resize(m_gpuCacheRowPitch * texHeight, 0);

    // Fill all entries
    for (u32 i = 0; i < probeCount; i++)
        WriteProbeToCache(i);

    Msg("* [LightProbeGrid] GPU cache initialized: %dx%d (%.2f MB)",
        texWidth, texHeight, (float)(m_gpuCache.size()) / (1024.0f * 1024.0f));
}

void CLightProbeGrid::PrepareGPUBuffer()
{
    if (m_probes.empty())
    {
        Msg("* [LightProbeGrid] No probes to upload");
        return;
    }

    u32 probeCount = (u32)m_probes.size();

    const u32 MAX_TEXTURE_DIM = 16384;
    const u32 MAX_PROBES = PROBES_PER_ROW * MAX_TEXTURE_DIM;

    if (probeCount > MAX_PROBES)
    {
        Msg("! [LightProbeGrid] WARNING: Capping probes from %d to %d", probeCount, MAX_PROBES);
        probeCount = MAX_PROBES;
    }

    u32 texWidth = PROBES_PER_ROW * 4;
    u32 texHeight = (probeCount + PROBES_PER_ROW - 1) / PROBES_PER_ROW;

    // Reallocate if needed
    if (probeCount > m_gpuAllocatedProbes)
    {
        if (m_pProbeSRV) { m_pProbeSRV->Release(); m_pProbeSRV = nullptr; }
        if (m_pProbeTexture) { m_pProbeTexture->Release(); m_pProbeTexture = nullptr; }

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = texWidth;
        texDesc.Height = texHeight;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage = D3D11_USAGE_DYNAMIC;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        Msg("* [LightProbeGrid] Creating debug texture %dx%d for %d probes",
            texWidth, texHeight, probeCount);

        HRESULT hr = HW.pDevice->CreateTexture2D(&texDesc, nullptr, &m_pProbeTexture);
        if (FAILED(hr))
        {
            Msg("! [LightProbeGrid] CreateTexture2D failed with HRESULT 0x%08X", hr);
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        R_CHK(HW.pDevice->CreateShaderResourceView(m_pProbeTexture, &srvDesc, &m_pProbeSRV));

        m_gpuAllocatedProbes = probeCount;
    }

    // Upload from persistent GPU cache
    if (m_gpuBufferDirty && m_pProbeTexture && !m_gpuCache.empty())
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(HW.pContext->Map(m_pProbeTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            if (mapped.RowPitch == m_gpuCacheRowPitch)
            {
                memcpy(mapped.pData, m_gpuCache.data(), m_gpuCacheRowPitch * texHeight);
            }
            else
            {
                const u8* src = m_gpuCache.data();
                u8* dst = (u8*)mapped.pData;
                u32 copyWidth = _min(m_gpuCacheRowPitch, mapped.RowPitch);
                for (u32 row = 0; row < texHeight; row++)
                {
                    memcpy(dst + row * mapped.RowPitch, src + row * m_gpuCacheRowPitch, copyWidth);
                }
            }

            HW.pContext->Unmap(m_pProbeTexture, 0);
        }

        m_gpuBufferDirty = false;
    }
}

void CLightProbeGrid::BindToShader(u32 slot)
{
    if (m_pProbeSRV)
    {
        HW.pContext->PSSetShaderResources(slot, 1, &m_pProbeSRV);
    }
}

bool CLightProbeGrid::SampleNearest(const Fvector& position, Fvector& outAmbient, float& outSkyVis)
{
    if (m_probes.empty()) return false;

    // Use spatial hash for O(1) lookup instead of O(N) linear scan
    if (!m_spatialHash.empty())
    {
        float minDistSq = FLT_MAX;
        const CLightProbe* nearest = nullptr;
        Ivector centerCell = WorldToHashCell(position);

        // Check 3×3×3 neighborhood (max 108 probes)
        for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
            int cx = centerCell.x + dx;
            int cy = centerCell.y + dy;
            int cz = centerCell.z + dz;

            if (cx < 0 || cx >= m_hashDims.x) continue;
            if (cy < 0 || cy >= m_hashDims.y) continue;
            if (cz < 0 || cz >= m_hashDims.z) continue;

            int cellIndex = cz * (m_hashDims.x * m_hashDims.y)
                          + cy * m_hashDims.x
                          + cx;

            const SpatialHashCell& cell = m_spatialHash[cellIndex];
            for (u8 p = 0; p < cell.count; p++)
            {
                u32 idx = cell.probeIndices[p];
                if (idx >= m_probes.size()) continue;

                float distSq = position.distance_to_sqr(m_probes[idx].position);
                if (distSq < minDistSq)
                {
                    minDistSq = distSq;
                    nearest = &m_probes[idx];
                }
            }
        }

        if (nearest)
        {
            outAmbient = nearest->ambient;
            outSkyVis = nearest->skyVisibility;
            return true;
        }
    }

    return false;
}

Fvector CLightProbeGrid::GetBoundsMin() const
{
    return m_boundsMin;
}

Fvector CLightProbeGrid::GetBoundsMax() const
{
    return m_boundsMax;
}

Ivector CLightProbeGrid::GetDimensions() const
{
    return m_gridDims;
}

//////////////////////////////////////////////////////////////////////////
// Proximity Check
//////////////////////////////////////////////////////////////////////////

bool CLightProbeGrid::HasNearbyProbe(const Fvector& pos, float minDist) const
{
    // Use spatial hash for O(1) proximity check
    if (m_spatialHash.empty()) return false;

    float minDistSq = minDist * minDist;
    Ivector center = WorldToHashCell(pos);

    // Check 3x3x3 neighborhood
    for (int dz = -1; dz <= 1; dz++)
    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
    {
        int cx = center.x + dx;
        int cy = center.y + dy;
        int cz = center.z + dz;

        if (cx < 0 || cx >= m_hashDims.x) continue;
        if (cy < 0 || cy >= m_hashDims.y) continue;
        if (cz < 0 || cz >= m_hashDims.z) continue;

        int cellIndex = cz * (m_hashDims.x * m_hashDims.y)
                      + cy * m_hashDims.x
                      + cx;

        const SpatialHashCell& cell = m_spatialHash[cellIndex];
        for (u8 i = 0; i < cell.count; i++)
        {
            u32 pi = cell.probeIndices[i];
            if (pi == 0xFFFFFFFF || pi >= m_probes.size()) continue;

            float distSq = pos.distance_to_sqr(m_probes[pi].position);
            if (distSq < minDistSq)
                return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Spatial Hash Implementation (CPU-side only)
//////////////////////////////////////////////////////////////////////////

Ivector CLightProbeGrid::WorldToHashCell(const Fvector& pos) const
{
    Fvector local;
    local.sub(pos, m_hashMin);

    Ivector cell;
    cell.x = _max(0, _min((int)(local.x / m_hashCellSize), m_hashDims.x - 1));
    cell.y = _max(0, _min((int)(local.y / m_hashCellSize), m_hashDims.y - 1));
    cell.z = _max(0, _min((int)(local.z / m_hashCellSize), m_hashDims.z - 1));

    return cell;
}

void CLightProbeGrid::BuildSpatialHash()
{
    if (m_probes.empty()) return;

    // Use bounds with a small margin
    Fvector margin = { 0.5f, 0.5f, 0.5f };
    m_hashMin.sub(m_boundsMin, margin);

    Fvector hashMax;
    hashMax.add(m_boundsMax, margin);

    // Compute hash grid dimensions
    Fvector extent;
    extent.sub(hashMax, m_hashMin);

    m_hashDims.x = _max(1, (int)ceilf(extent.x / m_hashCellSize));
    m_hashDims.y = _max(1, (int)ceilf(extent.y / m_hashCellSize));
    m_hashDims.z = _max(1, (int)ceilf(extent.z / m_hashCellSize));

    // Allocate and clear hash grid
    u32 totalCells = m_hashDims.x * m_hashDims.y * m_hashDims.z;
    m_spatialHash.resize(totalCells);

    for (auto& cell : m_spatialHash)
    {
        cell.count = 0;
        for (int i = 0; i < MAX_PROBES_PER_CELL; i++)
            cell.probeIndices[i] = 0xFFFFFFFF;
    }

    // Insert each probe into its cell
    for (u32 i = 0; i < m_probes.size(); i++)
    {
        Ivector cellCoord = WorldToHashCell(m_probes[i].position);
        int cellIndex = cellCoord.z * (m_hashDims.x * m_hashDims.y)
                      + cellCoord.y * m_hashDims.x
                      + cellCoord.x;

        if (cellIndex >= 0 && cellIndex < (int)m_spatialHash.size())
        {
            SpatialHashCell& cell = m_spatialHash[cellIndex];
            if (cell.count < MAX_PROBES_PER_CELL)
            {
                cell.probeIndices[cell.count] = i;
                cell.count++;
            }
        }
    }
}

