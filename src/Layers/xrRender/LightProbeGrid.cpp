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
    , m_pHashTexture(nullptr)
    , m_pHashSRV(nullptr)
    , m_hashDirty(true)
    , m_hashCellSize(DEFAULT_HASH_CELL_SIZE)
    , m_propagationIters(DEFAULT_PROPAGATION_ITERS)
    , m_propagationRate(DEFAULT_PROPAGATION_RATE)
{
    m_boundsMin.set(0, 0, 0);
    m_boundsMax.set(0, 0, 0);
    m_gridDims.set(0, 0, 0);
    m_hashMin.set(0, 0, 0);
    m_hashDims.set(0, 0, 0);

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
    m_spatialHash.clear();
    m_probeNeighbors.clear();

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
    if (m_pHashSRV)
    {
        m_pHashSRV->Release();
        m_pHashSRV = nullptr;
    }
    if (m_pHashTexture)
    {
        m_pHashTexture->Release();
        m_pHashTexture = nullptr;
    }

    m_gpuTextureHeight = 0;
    m_gpuBufferDirty = true;
    m_hashDirty = true;
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

    // Compute grid bounds first (needed for spatial hash)
    ComputeGridBounds();

    // Build spatial hash acceleration structure
    Msg("* [LightProbeGrid] Building spatial hash...");
    BuildSpatialHash();

    // Build neighbor connectivity for light propagation
    Msg("* [LightProbeGrid] Building neighbor connectivity...");
    BuildNeighborConnectivity();

    // Initial full update
    Msg("* [LightProbeGrid] Performing initial probe update...");
    for (u32 i = 0; i < m_probes.size(); i++)
        UpdateProbe(m_probes[i], i);

    // Initial propagation pass
    PropagateLight(m_propagationIters);

    m_gpuBufferDirty = true;
    m_hashDirty = true;
    PrepareGPUBuffer();
    PrepareHashGPUBuffer();

    Msg("* [LightProbeGrid] Placed %d probes, bounds (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)",
        m_probes.size(),
        m_boundsMin.x, m_boundsMin.y, m_boundsMin.z,
        m_boundsMax.x, m_boundsMax.y, m_boundsMax.z);
    Msg("* [LightProbeGrid] Spatial hash: %dx%dx%d cells (%.1fm cell size)",
        m_hashDims.x, m_hashDims.y, m_hashDims.z, m_hashCellSize);
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

void CLightProbeGrid::UpdateProbe(CLightProbe& probe, u32 probeIndex)
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
    sunDir.invert();  // env.sun_dir points FROM sun TO ground; we need toward-sun direction

    CDB::MODEL* staticModel = g_pGameLevel ? g_pGameLevel->ObjectSpace.GetStaticModel() : nullptr;
    if (!staticModel) return;

    m_collider.ray_options(CDB::OPT_ONLYNEAREST);

    // Track received sunlight from hemisphere samples
    float receivedSunlight = 0;

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

            // Check if this sky ray is toward the sun (receiving direct sunlight through opening)
            float sunAlignment = dir.dotproduct(sunDir);
            if (sunAlignment > SUN_ALIGNMENT_THRESHOLD)
            {
                // Weight by how closely aligned with sun direction
                receivedSunlight += sunAlignment;
            }
        }
        else
        {
            // Ray hit geometry - compute bounce
            CDB::RESULT* hit = m_collider.r_begin();
            Fvector hitNormal = ComputeTriangleNormal(*hit);
            Fvector hitPos;
            hitPos.mad(probe.position, dir, hit->range);

            CastBounceRay(hitPos, hitNormal, bounceAccum, sunDir, sunColor);

            // Check if the surface we hit is sunlit (receiving reflected sunlight)
            m_collider.ray_query(staticModel, hitPos, sunDir, RAY_MAX_DISTANCE);
            if (m_collider.r_count() == 0)
            {
                // Hit surface can see sun - we're receiving reflected sunlight!
                float NdotL = hitNormal.dotproduct(sunDir);
                if (NdotL > 0)
                {
                    // Weight by surface's sun-facing angle and inverse distance
                    float hitDist = hit->range;
                    float distFactor = 1.0f / (1.0f + hitDist * 0.1f);
                    receivedSunlight += NdotL * distFactor * 0.5f;
                }
            }
        }
    }

    // Normalize received sunlight by ray count
    receivedSunlight = _min(receivedSunlight / RAYS_PER_PROBE, 1.0f);

    // Soft shadow sun visibility - cast multiple jittered rays toward sun
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

    // Cast jittered rays for soft shadows
    for (int i = 0; i < SOFT_SHADOW_RAYS; i++)
    {
        // Deterministic jitter pattern (Fibonacci-like spiral)
        float angle = (float)i * 2.399f;  // Golden angle in radians
        float radius = SOFT_SHADOW_JITTER * (0.3f + 0.7f * (float)i / (float)SOFT_SHADOW_RAYS);

        Fvector jitteredDir;
        jitteredDir.set(sunDir);
        jitteredDir.mad(sunTangent, cosf(angle) * radius);
        jitteredDir.mad(sunBitangent, sinf(angle) * radius);
        jitteredDir.normalize();

        m_collider.ray_query(staticModel, probe.position, jitteredDir, RAY_MAX_DISTANCE);
        if (m_collider.r_count() == 0)
        {
            directSunVis += 1.0f / SOFT_SHADOW_RAYS;
        }
    }

    // Combine direct sun visibility with received sunlight
    // Direct visibility takes priority, but received light fills in for indirect cases
    float combinedSunVis = _max(directSunVis, receivedSunlight * RECEIVED_LIGHT_WEIGHT);
    probe.sunVisibility = combinedSunVis;

    // Gather bounce light from sunlit neighbors (Phase 4: Sunlit Bounce)
    if (probeIndex < m_probeNeighbors.size())
    {
        const ProbeNeighbors& neighbors = m_probeNeighbors[probeIndex];
        Fvector sunlitBounce = { 0, 0, 0 };
        float sunlitWeight = 0;

        for (int n = 0; n < 6; n++)
        {
            if (neighbors.indices[n] == 0xFFFF) continue;

            const CLightProbe& neighbor = m_probes[neighbors.indices[n]];

            // Only contribute if neighbor is sunlit
            if (neighbor.sunVisibility > 0.3f)
            {
                float dist = neighbors.distances[n];
                // Weight by sun visibility and inverse distance squared
                float weight = neighbor.sunVisibility / (1.0f + dist * dist * 0.1f);

                // Use neighbor's ambient as bounce source
                sunlitBounce.mad(neighbor.ambient, weight);
                sunlitWeight += weight;
            }
        }

        if (sunlitWeight > 0)
        {
            sunlitBounce.div(sunlitWeight);
            sunlitBounce.mul(m_bounceIntensity * 0.5f);  // Half intensity for neighbor bounce
            bounceAccum.add(sunlitBounce);
        }
    }

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
            UpdateProbe(probe, idx);
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

        UpdateProbe(probe, idx);
        updated++;
    }

    // Periodic light propagation pass
    if (m_currentFrame % m_propagationRate == 0)
    {
        PropagateLight(m_propagationIters);
    }

    m_nextProbeIndex = (m_nextProbeIndex + m_updateBudget) % _max(1u, (u32)m_probes.size());
    m_lastUpdateTimeMs = updateTimer.GetElapsed_sec() * 1000.0f;

    // Upload updated data to GPU
    if (m_gpuBufferDirty)
        PrepareGPUBuffer();
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

Fvector CLightProbeGrid::GetHashMin() const
{
    return m_hashMin;
}

Ivector CLightProbeGrid::GetHashDimensions() const
{
    return m_hashDims;
}

//////////////////////////////////////////////////////////////////////////
// Spatial Hash Implementation
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
            cell.probeIndices[i] = 0xFFFF;
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
                cell.probeIndices[cell.count] = (u16)i;
                cell.count++;
            }
        }
    }

    m_hashDirty = true;
}

void CLightProbeGrid::PrepareHashGPUBuffer()
{
    if (m_spatialHash.empty()) return;

    u32 totalCells = (u32)m_spatialHash.size();

    // Texture layout: 1D array of cells, 2 texels per cell (8 probe indices)
    // Using R16G16B16A16_UINT format: 4 uint16 per texel
    // Texel 0: indices[0-3], Texel 1: indices[4-7]
    u32 texWidth = totalCells * 2;  // 2 texels per cell
    u32 texHeight = 1;

    // Check if we need to wrap into 2D (max texture width is typically 16384)
    const u32 MAX_WIDTH = 16384;
    if (texWidth > MAX_WIDTH)
    {
        texHeight = (texWidth + MAX_WIDTH - 1) / MAX_WIDTH;
        texWidth = MAX_WIDTH;
    }

    // Create texture if needed
    if (!m_pHashTexture || m_hashDirty)
    {
        if (m_pHashSRV) { m_pHashSRV->Release(); m_pHashSRV = nullptr; }
        if (m_pHashTexture) { m_pHashTexture->Release(); m_pHashTexture = nullptr; }

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = texWidth;
        texDesc.Height = texHeight;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R16G16B16A16_UINT;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage = D3D11_USAGE_DYNAMIC;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = HW.pDevice->CreateTexture2D(&texDesc, nullptr, &m_pHashTexture);
        if (FAILED(hr))
        {
            Msg("! [LightProbeGrid] Hash texture creation failed: 0x%08X", hr);
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_UINT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        R_CHK(HW.pDevice->CreateShaderResourceView(m_pHashTexture, &srvDesc, &m_pHashSRV));
    }

    // Upload data
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(HW.pContext->Map(m_pHashTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        u16* data = (u16*)mapped.pData;

        for (u32 c = 0; c < totalCells; c++)
        {
            const SpatialHashCell& cell = m_spatialHash[c];

            // Calculate 2D position
            u32 linearPos = c * 2;
            u32 row = linearPos / texWidth;
            u32 col = linearPos % texWidth;

            u16* rowPtr = (u16*)((u8*)mapped.pData + row * mapped.RowPitch);

            // Texel 0: indices 0-3
            rowPtr[col * 4 + 0] = cell.probeIndices[0];
            rowPtr[col * 4 + 1] = cell.probeIndices[1];
            rowPtr[col * 4 + 2] = cell.probeIndices[2];
            rowPtr[col * 4 + 3] = cell.probeIndices[3];

            // Texel 1: indices 4-7
            if (col + 1 < texWidth)
            {
                rowPtr[(col + 1) * 4 + 0] = cell.probeIndices[4];
                rowPtr[(col + 1) * 4 + 1] = cell.probeIndices[5];
                rowPtr[(col + 1) * 4 + 2] = cell.probeIndices[6];
                rowPtr[(col + 1) * 4 + 3] = cell.probeIndices[7];
            }
            else
            {
                // Wrap to next row
                u16* nextRowPtr = (u16*)((u8*)mapped.pData + (row + 1) * mapped.RowPitch);
                nextRowPtr[0] = cell.probeIndices[4];
                nextRowPtr[1] = cell.probeIndices[5];
                nextRowPtr[2] = cell.probeIndices[6];
                nextRowPtr[3] = cell.probeIndices[7];
            }
        }

        HW.pContext->Unmap(m_pHashTexture, 0);
    }

    m_hashDirty = false;
}

void CLightProbeGrid::BindHashToShader(u32 slot)
{
    if (m_pHashSRV)
    {
        HW.pContext->PSSetShaderResources(slot, 1, &m_pHashSRV);
    }
}

//////////////////////////////////////////////////////////////////////////
// Neighbor Connectivity Implementation
//////////////////////////////////////////////////////////////////////////

void CLightProbeGrid::BuildNeighborConnectivity()
{
    if (m_probes.empty()) return;

    m_probeNeighbors.resize(m_probes.size());

    // Direction vectors for +X, -X, +Y, -Y, +Z, -Z
    static const Fvector dirs[6] = {
        { 1, 0, 0 }, { -1, 0, 0 },
        { 0, 1, 0 }, { 0, -1, 0 },
        { 0, 0, 1 }, { 0, 0, -1 }
    };

    // Maximum distance to consider as neighbor (2.5x outdoor spacing)
    float maxNeighborDist = OUTDOOR_GRID_SPACING * 2.5f;

    for (u32 i = 0; i < m_probes.size(); i++)
    {
        ProbeNeighbors& neighbors = m_probeNeighbors[i];
        const Fvector& pos = m_probes[i].position;

        // Initialize as no neighbors
        for (int n = 0; n < 6; n++)
        {
            neighbors.indices[n] = 0xFFFF;
            neighbors.distances[n] = FLT_MAX;
        }

        // Use spatial hash to find candidate neighbors efficiently
        Ivector centerCell = WorldToHashCell(pos);

        // Check 3x3x3 neighborhood of hash cells
        for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
            Ivector checkCell;
            checkCell.x = centerCell.x + dx;
            checkCell.y = centerCell.y + dy;
            checkCell.z = centerCell.z + dz;

            // Bounds check
            if (checkCell.x < 0 || checkCell.x >= m_hashDims.x) continue;
            if (checkCell.y < 0 || checkCell.y >= m_hashDims.y) continue;
            if (checkCell.z < 0 || checkCell.z >= m_hashDims.z) continue;

            int cellIndex = checkCell.z * (m_hashDims.x * m_hashDims.y)
                          + checkCell.y * m_hashDims.x
                          + checkCell.x;

            const SpatialHashCell& cell = m_spatialHash[cellIndex];

            for (int p = 0; p < cell.count; p++)
            {
                u32 j = cell.probeIndices[p];
                if (j == i) continue;  // Skip self

                Fvector delta;
                delta.sub(m_probes[j].position, pos);
                float dist = delta.magnitude();

                // Skip probes too far away
                if (dist > maxNeighborDist) continue;

                delta.normalize_safe();

                // Check which direction this neighbor is in
                for (int n = 0; n < 6; n++)
                {
                    float alignment = delta.dotproduct(dirs[n]);
                    // Must be mostly aligned (>0.7 = ~45 degrees) and closer than current
                    if (alignment > 0.7f && dist < neighbors.distances[n])
                    {
                        neighbors.indices[n] = (u16)j;
                        neighbors.distances[n] = dist;
                    }
                }
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Light Propagation Implementation
//////////////////////////////////////////////////////////////////////////

void CLightProbeGrid::PropagateLight(int iterations)
{
    if (m_probes.empty() || m_probeNeighbors.empty()) return;

    // Temporary buffer for ping-pong
    xr_vector<Fvector> newAmbient(m_probes.size());

    for (int iter = 0; iter < iterations; iter++)
    {
        for (u32 i = 0; i < m_probes.size(); i++)
        {
            const ProbeNeighbors& neighbors = m_probeNeighbors[i];
            Fvector neighborContrib = { 0, 0, 0 };
            float totalWeight = 0;

            for (int n = 0; n < 6; n++)
            {
                if (neighbors.indices[n] == 0xFFFF) continue;

                const CLightProbe& neighbor = m_probes[neighbors.indices[n]];
                float dist = neighbors.distances[n];
                float weight = 1.0f / (1.0f + dist);

                neighborContrib.mad(neighbor.ambient, weight);
                totalWeight += weight;
            }

            if (totalWeight > 0)
            {
                neighborContrib.div(totalWeight);
                // Blend: 85% self, 15% neighbors
                newAmbient[i].lerp(m_probes[i].ambient, neighborContrib, 0.15f);
            }
            else
            {
                newAmbient[i] = m_probes[i].ambient;
            }
        }

        // Copy back for next iteration
        for (u32 i = 0; i < m_probes.size(); i++)
            m_probes[i].ambient = newAmbient[i];
    }

    m_gpuBufferDirty = true;
}
