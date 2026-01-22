#include "stdafx.h"
#include "LightProbeGrid.h"
#include "xrRender_console.h"
#include "r__sector.h"
#include "FBasicVisual.h"
#include "../../xrEngine/xr_object.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/IGame_Level.h"
#include "../../xrEngine/Environment.h"

// External console variables
extern int   ps_r_probe_update_rate;
extern float ps_r_probe_bounce_intensity;
extern int   ps_r_debug_probes;
extern int   ps_r3_ssfx_il;

// Global instance
CLightProbeGrid* g_LightProbeGrid = nullptr;

//////////////////////////////////////////////////////////////////////////
// Fibonacci Hemisphere Rays
//////////////////////////////////////////////////////////////////////////
static Fvector s_hemisphereRays[RAYS_PER_PROBE];
static bool    s_raysInitialized = false;

static void InitializeHemisphereRays()
{
    if (s_raysInitialized) return;

    const float phi = (1.0f + sqrtf(5.0f)) / 2.0f;  // Golden ratio

    for (int i = 0; i < RAYS_PER_PROBE; i++)
    {
        float y = 1.0f - (float(i) / float(RAYS_PER_PROBE - 1));
        y = y * 0.9f + 0.1f;  // Avoid pure horizontal

        float radius = sqrtf(1.0f - y * y);
        float theta = 2.0f * PI * float(i) / phi;

        s_hemisphereRays[i].x = cosf(theta) * radius;
        s_hemisphereRays[i].y = y;
        s_hemisphereRays[i].z = sinf(theta) * radius;
        s_hemisphereRays[i].normalize();
    }
    s_raysInitialized = true;
}

//////////////////////////////////////////////////////////////////////////
// CLightProbeGrid Implementation
//////////////////////////////////////////////////////////////////////////

CLightProbeGrid::CLightProbeGrid()
    : m_pProbeTexture(nullptr)
    , m_pProbeSRV(nullptr)
    , m_gpuBufferDirty(true)
    , m_gpuTextureHeight(0)
    , m_updateBudget(50)
    , m_nextProbeIndex(0)
    , m_currentFrame(0)
    , m_bounceIntensity(DEFAULT_BOUNCE_INTENSITY)
    , m_lastUpdateTimeMs(0)
    , m_debugEnabled(false)
{
    m_boundsMin.set(0, 0, 0);
    m_boundsMax.set(0, 0, 0);
    m_gridDims.set(0, 0, 0);

    InitializeHemisphereRays();
}

CLightProbeGrid::~CLightProbeGrid()
{
    Clear();
}

void CLightProbeGrid::Clear()
{
    m_probes.clear();
    m_visibleSectors.clear();

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

    m_gpuTextureHeight = 0;
    m_gpuBufferDirty = true;
    m_nextProbeIndex = 0;
}

bool CLightProbeGrid::IsSectorIndoor(CSector* sector)
{
    if (!sector) return false;

    // Simple heuristic: cast rays up from sector center to check sky visibility
    // If most rays hit geometry, it's likely indoor
    dxRender_Visual* visual = sector->root();
    if (!visual) return false;

    Fbox bounds = visual->getVisData().box;
    Fvector center;
    bounds.getcenter(center);

    if (!g_pGameLevel) return false;
    CDB::MODEL* staticModel = g_pGameLevel->ObjectSpace.GetStaticModel();
    if (!staticModel) return false;

    // Cast a few rays upward
    int hits = 0;
    m_collider.ray_options(CDB::OPT_ONLYNEAREST);

    Fvector upDirs[4] = {
        { 0, 1, 0 },
        { 0.3f, 0.95f, 0 },
        { -0.3f, 0.95f, 0 },
        { 0, 0.95f, 0.3f }
    };

    for (int i = 0; i < 4; i++)
    {
        upDirs[i].normalize();
        m_collider.ray_query(staticModel, center, upDirs[i], 50.0f);
        if (m_collider.r_count() > 0)
            hits++;
    }

    // If 3+ rays hit, consider it indoor
    return hits >= 3;
}

void CLightProbeGrid::PlaceProbesInSector(CSector* sector, u32 sectorIndex, bool isIndoor)
{
    if (!sector) return;

    float spacing = isIndoor ? INDOOR_GRID_SPACING : OUTDOOR_GRID_SPACING;

    // Use the sector's root visual for bounds
    dxRender_Visual* visual = sector->root();
    if (!visual) return;

    Fbox bounds = visual->getVisData().box;
    Fvector boundsMin = bounds.min;
    Fvector boundsMax = bounds.max;

    // Grid the sector volume
    for (float x = boundsMin.x; x <= boundsMax.x; x += spacing)
    {
        for (float y = boundsMin.y; y <= boundsMax.y; y += spacing)
        {
            for (float z = boundsMin.z; z <= boundsMax.z; z += spacing)
            {
                Fvector pos = { x, y, z };

                if (!IsValidProbePosition(pos))
                    continue;

                CLightProbe probe;
                probe.position = pos;
                probe.skyVisibility = 0.5f;  // Initial guess
                probe.ambient.set(0.1f, 0.1f, 0.1f);
                probe.sunVisibility = 0.5f;
                probe.bounce.set(0, 0, 0);
                probe.sectorId = (u16)(sectorIndex & 0xFFFF);  // Use index as ID
                probe.lastUpdateFrame = 0;

                m_probes.push_back(probe);
            }
        }
    }
}

void CLightProbeGrid::PlacePortalBridgeProbes(CPortal* portal)
{
    if (!portal) return;

    // Get portal center from bounding sphere and normal from plane
    Fvector center = portal->S.P;  // Sphere center
    Fvector normal = portal->P.n;  // Plane normal

    // Place probes on both sides of the portal
    CLightProbe probe1, probe2;

    probe1.position.mad(center, normal, PORTAL_BRIDGE_OFFSET);
    probe1.skyVisibility = 0.5f;
    probe1.ambient.set(0.1f, 0.1f, 0.1f);
    probe1.sunVisibility = 0.5f;
    probe1.bounce.set(0, 0, 0);
    probe1.sectorId = 0xFFFF;  // Special portal marker
    probe1.lastUpdateFrame = 0;

    probe2.position.mad(center, normal, -PORTAL_BRIDGE_OFFSET);
    probe2.skyVisibility = 0.5f;
    probe2.ambient.set(0.1f, 0.1f, 0.1f);
    probe2.sunVisibility = 0.5f;
    probe2.bounce.set(0, 0, 0);
    probe2.sectorId = 0xFFFF;
    probe2.lastUpdateFrame = 0;

    if (IsValidProbePosition(probe1.position))
        m_probes.push_back(probe1);
    if (IsValidProbePosition(probe2.position))
        m_probes.push_back(probe2);
}

bool CLightProbeGrid::IsValidProbePosition(const Fvector& pos)
{
    if (!g_pGameLevel) return false;

    CDB::MODEL* staticModel = g_pGameLevel->ObjectSpace.GetStaticModel();
    if (!staticModel) return false;

    // Check if position is inside geometry by casting ray down
    m_collider.ray_options(CDB::OPT_ONLYNEAREST);
    m_collider.ray_query(staticModel, pos, Fvector().set(0, -1, 0), 2.0f);

    // Valid if ray hit something below (not floating in air)
    if (m_collider.r_count() == 0)
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
        m_collider.ray_query(staticModel, pos, dirs[i], 0.3f);
        if (m_collider.r_count() > 0)
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

    // Initialize rays if not done
    InitializeHemisphereRays();

    // Place probes in each sector
    for (u32 i = 0; i < RImplementation.Sectors.size(); i++)
    {
        CSector* sector = (CSector*)RImplementation.Sectors[i];
        if (!sector) continue;

        bool isIndoor = IsSectorIndoor(sector);
        PlaceProbesInSector(sector, i, isIndoor);
    }

    // Place portal bridge probes
    for (u32 i = 0; i < RImplementation.Portals.size(); i++)
    {
        CPortal* portal = (CPortal*)RImplementation.Portals[i];
        if (!portal) continue;

        PlacePortalBridgeProbes(portal);
    }

    Msg("* [LightProbeGrid] Placed %d probes from %d sectors and %d portals",
        m_probes.size(), RImplementation.Sectors.size(), RImplementation.Portals.size());

    if (m_probes.empty())
    {
        Msg("* [LightProbeGrid] No probes placed - skipping build");
        return;
    }

    // Initial full update
    Msg("* [LightProbeGrid] Performing initial probe update...");
    for (auto& probe : m_probes)
        UpdateProbe(probe);

    ComputeGridBounds();
    m_gpuBufferDirty = true;
    PrepareGPUBuffer();

    Msg("* [LightProbeGrid] Placed %d probes, bounds (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)",
        m_probes.size(),
        m_boundsMin.x, m_boundsMin.y, m_boundsMin.z,
        m_boundsMax.x, m_boundsMax.y, m_boundsMax.z);
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

Fvector CLightProbeGrid::ComputeTriangleNormal(const CDB::RESULT& hit)
{
    CDB::MODEL* staticModel = g_pGameLevel->ObjectSpace.GetStaticModel();
    if (!staticModel)
        return Fvector().set(0, 1, 0);

    CDB::TRI* tris = staticModel->get_tris();
    Fvector* verts = staticModel->get_verts();

    CDB::TRI& tri = tris[hit.id];
    Fvector v0 = verts[tri.verts[0]];
    Fvector v1 = verts[tri.verts[1]];
    Fvector v2 = verts[tri.verts[2]];

    Fvector e1, e2, normal;
    e1.sub(v1, v0);
    e2.sub(v2, v0);
    normal.crossproduct(e1, e2);
    normal.normalize_safe();

    return normal;
}

void CLightProbeGrid::CastBounceRay(const Fvector& hitPos, const Fvector& hitNormal,
                                     Fvector& bounceAccum, const Fvector& sunDir, const Fvector& sunColor)
{
    if (!g_pGameLevel) return;

    CDB::MODEL* staticModel = g_pGameLevel->ObjectSpace.GetStaticModel();
    if (!staticModel) return;

    // Check if sun is visible from hit point
    m_collider.ray_options(CDB::OPT_ONLYNEAREST);
    m_collider.ray_query(staticModel, hitPos, sunDir, RAY_MAX_DISTANCE);

    if (m_collider.r_count() == 0)
    {
        // Sun visible - compute Lambertian bounce
        float NdotL = hitNormal.dotproduct(sunDir);
        if (NdotL > 0)
        {
            Fvector contribution;
            contribution.set(sunColor);
            contribution.mul(NdotL * ASSUMED_ALBEDO * m_bounceIntensity);
            bounceAccum.add(contribution);
        }
    }
}

void CLightProbeGrid::BuildVisibleSectorSet()
{
    m_visibleSectors.clear();

    // Get visible sectors from portal traverser
    // r_sectors contains IRender_Sector* but they're actually CSector*
    for (u32 i = 0; i < PortalTraverser.r_sectors.size(); i++)
    {
        IRender_Sector* isector = PortalTraverser.r_sectors[i];
        if (!isector) continue;

        // Find the index of this sector in RImplementation.Sectors
        for (u32 j = 0; j < RImplementation.Sectors.size(); j++)
        {
            if (RImplementation.Sectors[j] == isector)
            {
                m_visibleSectors.insert((u16)j);
                break;
            }
        }
    }
}

bool CLightProbeGrid::IsProbeInVisibleSector(const CLightProbe& probe)
{
    if (probe.sectorId == 0xFFFF)  // Portal probe - always update
        return true;
    return m_visibleSectors.find(probe.sectorId) != m_visibleSectors.end();
}

void CLightProbeGrid::UpdateProbe(CLightProbe& probe)
{
    probe.lastUpdateFrame = (u16)(m_currentFrame & 0xFFFF);

    float skyHits = 0;
    float totalRays = 0;
    Fvector ambientAccum = { 0, 0, 0 };
    Fvector bounceAccum = { 0, 0, 0 };

    // Get current environment data
    if (!g_pGamePersistent || !g_pGamePersistent->Environment().CurrentEnv)
        return;

    CEnvDescriptorMixer& env = *g_pGamePersistent->Environment().CurrentEnv;
    Fvector skyColor;
    skyColor.set(env.hemi_color.x, env.hemi_color.y, env.hemi_color.z);
    Fvector sunColor;
    sunColor.set(env.sun_color.x, env.sun_color.y, env.sun_color.z);
    Fvector sunDir;
    sunDir.set(env.sun_dir.x, env.sun_dir.y, env.sun_dir.z);
    sunDir.normalize_safe();

    CDB::MODEL* staticModel = g_pGameLevel ? g_pGameLevel->ObjectSpace.GetStaticModel() : nullptr;
    if (!staticModel) return;

    m_collider.ray_options(CDB::OPT_ONLYNEAREST);

    // Hemisphere rays
    for (int i = 0; i < RAYS_PER_PROBE; i++)
    {
        const Fvector& dir = s_hemisphereRays[i];
        m_collider.ray_query(staticModel, probe.position, dir, RAY_MAX_DISTANCE);
        totalRays += 1.0f;

        if (m_collider.r_count() == 0)
        {
            // Ray escaped to sky
            skyHits += 1.0f;
            ambientAccum.add(skyColor);
        }
        else
        {
            // Ray hit geometry - compute bounce
            CDB::RESULT* hit = m_collider.r_begin();
            Fvector hitNormal = ComputeTriangleNormal(*hit);
            Fvector hitPos;
            hitPos.mad(probe.position, dir, hit->range);

            CastBounceRay(hitPos, hitNormal, bounceAccum, sunDir, sunColor);
        }
    }

    // Sun visibility
    m_collider.ray_query(staticModel, probe.position, sunDir, RAY_MAX_DISTANCE);
    probe.sunVisibility = (m_collider.r_count() == 0) ? 1.0f : 0.0f;

    // Finalize with temporal smoothing (70% old, 30% new)
    float newSkyVis = totalRays > 0 ? (skyHits / totalRays) : 0.0f;
    probe.skyVisibility = probe.skyVisibility * 0.7f + newSkyVis * 0.3f;

    Fvector newAmbient;
    if (totalRays > 0)
        newAmbient.set(ambientAccum).div(totalRays);
    else
        newAmbient.set(0, 0, 0);
    newAmbient.add(bounceAccum);

    probe.ambient.lerp(probe.ambient, newAmbient, 0.3f);
    probe.bounce.lerp(probe.bounce, bounceAccum, 0.3f);

    m_gpuBufferDirty = true;
}

void CLightProbeGrid::Update()
{
    if (m_probes.empty()) return;

    m_currentFrame++;
    m_updateBudget = (u32)ps_r_probe_update_rate;
    m_bounceIntensity = ps_r_probe_bounce_intensity;
    m_debugEnabled = ps_r_debug_probes != 0;

    CTimer updateTimer;
    updateTimer.Start();

    // Build visible sector set for prioritization
    BuildVisibleSectorSet();

    u32 updated = 0;

    // First pass: update visible sector probes (75% of budget)
    u32 visibleBudget = (m_updateBudget * 3) / 4;
    for (u32 i = 0; i < m_probes.size() && updated < visibleBudget; i++)
    {
        u32 idx = (m_nextProbeIndex + i) % m_probes.size();
        CLightProbe& probe = m_probes[idx];

        if (IsProbeInVisibleSector(probe))
        {
            UpdateProbe(probe);
            updated++;
        }
    }

    // Second pass: update remaining probes
    for (u32 i = 0; i < m_probes.size() && updated < m_updateBudget; i++)
    {
        u32 idx = (m_nextProbeIndex + i) % m_probes.size();
        CLightProbe& probe = m_probes[idx];

        // Skip if already updated this frame
        if (probe.lastUpdateFrame == (u16)(m_currentFrame & 0xFFFF))
            continue;

        UpdateProbe(probe);
        updated++;
    }

    m_nextProbeIndex = (m_nextProbeIndex + m_updateBudget) % _max(1u, (u32)m_probes.size());
    m_lastUpdateTimeMs = updateTimer.GetElapsed_sec() * 1000.0f;
}

void CLightProbeGrid::PrepareGPUBuffer()
{
    if (m_probes.empty())
    {
        Msg("* [LightProbeGrid] No probes to upload");
        return;
    }

    u32 probeCount = (u32)m_probes.size();

    // 2D texture layout: multiple probes per row to support millions of probes
    // Each probe uses 2 texels (position+skyVis, ambient+sunVis)
    // Width = PROBES_PER_ROW * 2, Height = ceil(probeCount / PROBES_PER_ROW)
    // Max capacity: 256 * 16384 = 4,194,304 probes
    const u32 MAX_TEXTURE_DIM = 16384;
    const u32 MAX_PROBES = PROBES_PER_ROW * MAX_TEXTURE_DIM;

    if (probeCount > MAX_PROBES)
    {
        Msg("! [LightProbeGrid] WARNING: Capping probes from %d to %d", probeCount, MAX_PROBES);
        probeCount = MAX_PROBES;
    }

    u32 texWidth = PROBES_PER_ROW * 2;  // 512 texels wide
    u32 texHeight = (probeCount + PROBES_PER_ROW - 1) / PROBES_PER_ROW;  // ceil division

    // Reallocate if needed
    if (probeCount > m_gpuTextureHeight)
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

        Msg("* [LightProbeGrid] Creating texture %dx%d for %d probes (%d probes/row)",
            texWidth, texHeight, probeCount, PROBES_PER_ROW);

        HRESULT hr = HW.pDevice->CreateTexture2D(&texDesc, nullptr, &m_pProbeTexture);
        if (FAILED(hr))
        {
            Msg("! [LightProbeGrid] CreateTexture2D failed with HRESULT 0x%08X (width=%d, height=%d)",
                hr, texWidth, texHeight);
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        R_CHK(HW.pDevice->CreateShaderResourceView(m_pProbeTexture, &srvDesc, &m_pProbeSRV));

        m_gpuTextureHeight = probeCount;
    }

    // Upload data if dirty
    if (m_gpuBufferDirty && m_pProbeTexture)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(HW.pContext->Map(m_pProbeTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            // 2D layout: PROBES_PER_ROW probes per texture row
            // Each probe uses 2 texels (8 floats = 32 bytes)
            // Probe i is at row (i / PROBES_PER_ROW), column (i % PROBES_PER_ROW) * 2
            u8* basePtr = (u8*)mapped.pData;
            const u32 texelSize = 4 * sizeof(float);  // 16 bytes per RGBA32F texel

            for (u32 i = 0; i < probeCount; i++)
            {
                u32 row = i / PROBES_PER_ROW;
                u32 col = (i % PROBES_PER_ROW) * 2;

                // Calculate pointer to this probe's texels
                u8* rowPtr = basePtr + row * mapped.RowPitch;
                float* texels = (float*)(rowPtr + col * texelSize);

                // Texel 0: position.xyz, skyVisibility
                texels[0] = m_probes[i].position.x;
                texels[1] = m_probes[i].position.y;
                texels[2] = m_probes[i].position.z;
                texels[3] = m_probes[i].skyVisibility;

                // Texel 1: ambient.xyz, sunVisibility
                texels[4] = m_probes[i].ambient.x;
                texels[5] = m_probes[i].ambient.y;
                texels[6] = m_probes[i].ambient.z;
                texels[7] = m_probes[i].sunVisibility;
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

    // Find nearest probe (simple linear search for now)
    float minDistSq = FLT_MAX;
    const CLightProbe* nearest = nullptr;

    for (const auto& probe : m_probes)
    {
        float distSq = position.distance_to_sqr(probe.position);
        if (distSq < minDistSq)
        {
            minDistSq = distSq;
            nearest = &probe;
        }
    }

    if (nearest)
    {
        outAmbient = nearest->ambient;
        outSkyVis = nearest->skyVisibility;
        return true;
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
