#include "stdafx.h"
#include "LightProbeGrid.h"
#include "xrRender_console.h"
#include "r__sector.h"
#include "../../xrEngine/xr_object.h"
#include "../../xrEngine/IGame_Persistent.h"
#include "../../xrEngine/IGame_Level.h"
#include "../../xrEngine/Environment.h"
#include "light.h"
#include "../../xrCDB/ISpatial.h"

// External console variables
extern int   ps_r_probe_update_rate;
extern float ps_r_probe_bounce_intensity;
extern int   ps_r_debug_probes;
extern float ps_r_probe_max_distance;
extern int   ps_r_probe_upload_rate;
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
    , m_gpuCacheRowPitch(0)
    , m_gpuCacheTexHeight(0)
    , m_updateBudget(50)
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
    , m_farRobinIndex(0)
    , m_lastUploadFrame(0)
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
    m_spatialHash.clear();
    m_probeNeighbors.clear();
    m_gpuCache.clear();
    m_gpuCacheRowPitch = 0;
    m_gpuCacheTexHeight = 0;
    m_propagationBuffer.clear();
    m_propagationActiveSet.clear();

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
    m_farRobinIndex = 0;
}

bool CLightProbeGrid::IsPositionIndoor(const Fvector& pos)
{
    if (!g_pGameLevel) return false;

    CDB::MODEL* staticModel = g_pGameLevel->ObjectSpace.GetStaticModel();
    if (!staticModel) return false;

    // Cast 5 upward rays from the candidate position
    // If 4+ rays hit geometry overhead, classify as indoor
    int hits = 0;
    m_collider.ray_options(CDB::OPT_ONLYNEAREST);

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
        m_collider.ray_query(staticModel, pos, upDirs[i], INDOOR_RAY_RANGE);
        if (m_collider.r_count() > 0)
            hits++;
    }

    return hits >= INDOOR_RAY_THRESHOLD;
}

CLightProbe CLightProbeGrid::MakeDefaultProbe(const Fvector& pos, bool isIndoor)
{
    CLightProbe probe;
    probe.position = pos;
    probe.skyVisibility = 0.0f;
    probe.ambient.set(0, 0, 0);
    probe.sunVisibility = 0.0f;
    probe.bounce.set(0, 0, 0);
    probe.dominantDir.set(0, 1, 0);
    probe.directionalRatio = 0.0f;
    probe.pointLightColor.set(0, 0, 0);
    probe.pointLightIntensity = 0.0f;
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
        m_probes.push_back(MakeDefaultProbe(pos1, false));
    if (IsValidProbePosition(pos2))
        m_probes.push_back(MakeDefaultProbe(pos2, false));
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

    if (!g_pGameLevel) return;

    // Get CFORM level bounds — the actual geometric extent of the level
    const Fbox& levelBounds = g_pGameLevel->ObjectSpace.GetBoundingVolume();
    Fvector bMin = levelBounds.min;
    Fvector bMax = levelBounds.max;

    Msg("* [LightProbeGrid] CFORM bounds: (%.1f,%.1f,%.1f) to (%.1f,%.1f,%.1f)",
        bMin.x, bMin.y, bMin.z, bMax.x, bMax.y, bMax.z);

    // =====================================================================
    // Pass 1: Outdoor sweep at OUTDOOR_GRID_SPACING (2.0m)
    // Covers the entire level uniformly; tags indoor positions for pass 2.
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

        m_probes.push_back(MakeDefaultProbe(pos, isIndoor));
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

        m_probes.push_back(MakeDefaultProbe(pos, true));
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

    // Build neighbor connectivity for light propagation
    Msg("* [LightProbeGrid] Building neighbor connectivity...");
    BuildNeighborConnectivity();

    // Initial full update of all probes
    Msg("* [LightProbeGrid] Performing initial probe update...");
    for (u32 i = 0; i < m_probes.size(); i++)
        UpdateProbe(m_probes[i], i);

    // Initial propagation pass
    PropagateLight(m_propagationIters);

    // Initialize persistent GPU cache and propagation buffers
    InitGPUCache();
    m_propagationBuffer.resize(m_probes.size());
    m_propagationActiveSet.reserve(m_probes.size());

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

u32 CLightProbeGrid::UpdateProbesInRadius(const Fvector& center, float minDist, float maxDist, u32 budget)
{
    if (budget == 0 || m_probes.empty()) return 0;

    float minDistSq = minDist * minDist;
    float maxDistSq = (maxDist < FLT_MAX) ? maxDist * maxDist : FLT_MAX;
    u32 updated = 0;

    // For near/medium tiers (small radius), iterate spatial hash cells within range.
    // For far/distant tiers (large radius), use round-robin to avoid iterating 100K+ cells.
    if (maxDist <= 60.0f && !m_spatialHash.empty())
    {
        int cellRadius = (int)ceilf(maxDist / m_hashCellSize) + 1;
        Ivector centerCell = WorldToHashCell(center);

        for (int dz = -cellRadius; dz <= cellRadius && updated < budget; dz++)
        for (int dy = -cellRadius; dy <= cellRadius && updated < budget; dy++)
        for (int dx = -cellRadius; dx <= cellRadius && updated < budget; dx++)
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
            for (u8 p = 0; p < cell.count && updated < budget; p++)
            {
                u32 idx = cell.probeIndices[p];
                if (idx >= m_probes.size()) continue;

                // Skip if already updated this frame
                if (m_probes[idx].lastUpdateFrame == (u16)(m_currentFrame & 0xFFFF))
                    continue;

                float distSq = center.distance_to_sqr(m_probes[idx].position);
                if (distSq < minDistSq || distSq >= maxDistSq) continue;

                UpdateProbe(m_probes[idx], idx);
                updated++;
            }
        }
    }
    else
    {
        // Round-robin scan with distance filter + view-cone culling for far probes
        u32 probeCount = (u32)m_probes.size();
        for (u32 i = 0; i < probeCount && updated < budget; i++)
        {
            u32 idx = (m_farRobinIndex + i) % probeCount;

            // Skip if already updated this frame
            if (m_probes[idx].lastUpdateFrame == (u16)(m_currentFrame & 0xFFFF))
                continue;

            float distSq = center.distance_to_sqr(m_probes[idx].position);
            if (distSq < minDistSq || distSq >= maxDistSq) continue;

            // View-cone culling for far tier (50m+): skip probes behind camera
            // Generous 214-degree cone (dot < -0.3) prevents artifacts when turning
            if (minDist >= 50.0f)
            {
                Fvector toProbe;
                toProbe.sub(m_probes[idx].position, center).normalize_safe();
                if (toProbe.dotproduct(Device.vCameraDirection) < -0.3f)
                    continue;
            }

            UpdateProbe(m_probes[idx], idx);
            updated++;
        }
        m_farRobinIndex = (m_farRobinIndex + budget) % _max(1u, (u32)m_probes.size());
    }

    return updated;
}

void CLightProbeGrid::UpdateProbe(CLightProbe& probe, u32 probeIndex)
{
    probe.lastUpdateFrame = (u16)(m_currentFrame & 0xFFFF);

    float skyHits = 0;
    float totalRays = 0;
    Fvector ambientAccum = { 0, 0, 0 };
    Fvector bounceAccum = { 0, 0, 0 };

    // Dominant direction accumulators
    Fvector dirAccum = { 0, 0, 0 };
    float   energyAccum = 0;

    // Point light accumulators
    Fvector pointLightAccum = { 0, 0, 0 };
    float   pointIntensityAccum = 0;

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

            // Sky direction contribution to dominant direction
            float skyLum = skyColor.x * 0.2126f + skyColor.y * 0.7152f + skyColor.z * 0.0722f;
            dirAccum.mad(dir, skyLum);
            energyAccum += skyLum;

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
            // Ray hit geometry - compute bounce with direction tracking
            CDB::RESULT* hit = m_collider.r_begin();
            Fvector hitNormal = ComputeTriangleNormal(*hit);
            Fvector hitPos;
            hitPos.mad(probe.position, dir, hit->range);

            // Capture bounce delta for direction accumulation
            Fvector bounceBefore = bounceAccum;
            CastBounceRay(hitPos, hitNormal, bounceAccum, sunDir, sunColor);
            Fvector bounceContrib;
            bounceContrib.sub(bounceAccum, bounceBefore);
            float bounceLum = bounceContrib.x * 0.2126f + bounceContrib.y * 0.7152f + bounceContrib.z * 0.0722f;
            if (bounceLum > 0)
            {
                dirAccum.mad(dir, bounceLum);
                energyAccum += bounceLum;
            }

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
            if (neighbors.indices[n] == 0xFFFFFFFF) continue;

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

    // =========================================================================
    // Point Light Injection
    // Query nearby point/spot lights via spatial DB, shadow-test, accumulate
    // =========================================================================
    if (g_SpatialSpace)
    {
        m_lightQueryResults.clear();
        g_SpatialSpace->q_sphere(m_lightQueryResults, 0,
            STYPE_LIGHTSOURCE | STYPE_LIGHTSOURCEHEMI,
            probe.position, POINT_LIGHT_SEARCH_RADIUS);

        int lightsProcessed = 0;
        for (u32 li = 0; li < m_lightQueryResults.size() && lightsProcessed < MAX_POINT_LIGHTS_PER_PROBE; li++)
        {
            ISpatial* spatial = m_lightQueryResults[li];
            if (!spatial) continue;
            IRender_Light* ilight = spatial->dcast_Light();
            if (!ilight) continue;
            light* L = (light*)ilight;
            if (!L->flags.bActive) continue;
            if (L->flags.type != IRender_Light::POINT && L->flags.type != IRender_Light::SPOT) continue;

            Fvector dirToLight;
            dirToLight.sub(L->position, probe.position);
            float distToLight = dirToLight.magnitude();
            if (distToLight >= L->range || distToLight < 0.01f) continue;
            dirToLight.div(distToLight);

            // D3D-style attenuation with range fade
            // Guard: uninitialized or zero attenuation values → denominator=0 → inf.
            // 0*inf = NaN (IEEE 754), which permanently contaminates probe data
            // through temporal smoothing (NaN lerp = NaN).
            float denom = L->attenuation0 + L->attenuation1 * distToLight
                        + L->attenuation2 * distToLight * distToLight;
            if (denom < 0.001f) continue;  // Skip lights with degenerate attenuation
            float atten = 1.0f / denom;
            float rangeFade = 1.0f - _min(distToLight / L->range, 1.0f);
            atten *= rangeFade * rangeFade;  // Quadratic fade at range boundary
            if (atten < 0.001f) continue;

            // Shadow test — is the light visible from the probe?
            m_collider.ray_options(CDB::OPT_ONLYNEAREST);
            m_collider.ray_query(staticModel, probe.position, dirToLight, distToLight - 0.05f);
            if (m_collider.r_count() > 0) continue;  // Occluded

            // Accumulate attenuated light color
            Fvector lightContrib;
            lightContrib.set(L->color.r, L->color.g, L->color.b);
            lightContrib.mul(atten);
            pointLightAccum.add(lightContrib);
            float lightLum = lightContrib.x * 0.2126f + lightContrib.y * 0.7152f + lightContrib.z * 0.0722f;
            pointIntensityAccum += lightLum;

            // NOTE: Point lights intentionally do NOT contribute to dirAccum/energyAccum.
            // They have their own dedicated channel (pointLightColor/Intensity).
            // Letting them influence directionalRatio causes artifacts: a bright campfire
            // pushes ratio→1.0, suppressing isotropic ambient. When the light turns off,
            // the inflated ratio persists via temporal smoothing, creating black holes
            // where the isotropic term is near-zero but the directional term points
            // at a now-dark light source.
            lightsProcessed++;
        }
    }

    // =========================================================================
    // Finalize dominant direction
    // =========================================================================
    Fvector newDominantDir;
    float newDirectionalRatio;
    float dirMag = dirAccum.magnitude();
    if (energyAccum > 0.001f && dirMag > 0.001f)
    {
        newDominantDir.set(dirAccum).div(dirMag);
        newDirectionalRatio = _min(dirMag / energyAccum, 1.0f);
    }
    else
    {
        newDominantDir.set(0, 1, 0);
        newDirectionalRatio = 0.0f;
    }

    // =========================================================================
    // Temporal smoothing (70% old, 30% new)
    // =========================================================================
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

    // Dominant direction: lerp then re-normalize (prevents vector shrinking)
    Fvector smoothedDir;
    smoothedDir.lerp(probe.dominantDir, newDominantDir, 0.3f);
    float smoothedMag = smoothedDir.magnitude();
    if (smoothedMag > 0.001f)
        probe.dominantDir.set(smoothedDir).div(smoothedMag);
    else
        probe.dominantDir.set(0, 1, 0);
    probe.directionalRatio = probe.directionalRatio * 0.7f + newDirectionalRatio * 0.3f;

    // Point light color and intensity
    // Sanitize: clamp to prevent inf/NaN from propagating through temporal smoothing
    pointLightAccum.x = _finite(pointLightAccum.x) ? _min(pointLightAccum.x, 100.0f) : 0.0f;
    pointLightAccum.y = _finite(pointLightAccum.y) ? _min(pointLightAccum.y, 100.0f) : 0.0f;
    pointLightAccum.z = _finite(pointLightAccum.z) ? _min(pointLightAccum.z, 100.0f) : 0.0f;
    pointIntensityAccum = _finite(pointIntensityAccum) ? _min(pointIntensityAccum, 100.0f) : 0.0f;
    // Sanitize existing probe data before lerp (NaN * 0.7 = NaN persists forever)
    if (!_finite(probe.pointLightColor.x) || !_finite(probe.pointLightColor.y) || !_finite(probe.pointLightColor.z))
        probe.pointLightColor.set(0, 0, 0);
    if (!_finite(probe.pointLightIntensity))
        probe.pointLightIntensity = 0.0f;

    // Slower blend for point lights (0.1 vs 0.3 for other fields) — acts as a
    // low-pass filter that smooths out campfire flicker and other rapid light
    // animation. Probes represent average illumination, not instantaneous.
    probe.pointLightColor.lerp(probe.pointLightColor, pointLightAccum, 0.1f);
    probe.pointLightIntensity = probe.pointLightIntensity * 0.9f + pointIntensityAccum * 0.1f;

    WriteProbeToCache(probeIndex);
    m_gpuBufferDirty = true;
}

void CLightProbeGrid::Update()
{
    if (m_probes.empty()) return;

    m_currentFrame++;
    m_updateBudget = (u32)ps_r_probe_update_rate;
    m_bounceIntensity = ps_r_probe_bounce_intensity;
    m_debugEnabled = ps_r_debug_probes != 0;

    float maxDist = ps_r_probe_max_distance;

    CTimer updateTimer;
    updateTimer.Start();

    Fvector playerPos = Device.vCameraPosition;

    // Distance-based tier budgets (distant tier eliminated — probes beyond maxDist keep Build() values)
    float farCap = _min(maxDist, 150.0f);  // Far tier capped at maxDist
    u32 nearBudget = (m_updateBudget * 50) / 100;   // 50% — 0-15m, every frame
    u32 medBudget  = (m_updateBudget * 30) / 100;   // 30% — 15-50m, every 4 frames
    u32 farBudget  = m_updateBudget - nearBudget - medBudget;  // 20% — 50m-maxDist, every 16 frames

    // Near tier: always update (most responsive to time-of-day changes)
    UpdateProbesInRadius(playerPos, 0.0f, 15.0f, nearBudget);

    // Medium tier: every 4 frames (capped at maxDist)
    if (m_currentFrame % 4 == 0)
        UpdateProbesInRadius(playerPos, 15.0f, _min(50.0f, maxDist), medBudget);

    // Far tier: every 16 frames (capped at maxDist, round-robin)
    if (m_currentFrame % 16 == 0 && maxDist > 50.0f)
        UpdateProbesInRadius(playerPos, 50.0f, farCap, farBudget);

    // Periodic light propagation pass
    if (m_currentFrame % m_propagationRate == 0)
        PropagateLight(m_propagationIters);

    m_lastUpdateTimeMs = updateTimer.GetElapsed_sec() * 1000.0f;

    // Throttled GPU upload — every N frames (from r_probe_upload_rate cvar)
    u32 uploadInterval = (u32)_max(1, ps_r_probe_upload_rate);
    if (m_gpuBufferDirty && (m_currentFrame - m_lastUploadFrame >= uploadInterval))
    {
        PrepareGPUBuffer();
        m_lastUploadFrame = m_currentFrame;
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

    // Texel 2: dominantDir.xyz, directionalRatio
    texels[8]  = probe.dominantDir.x;
    texels[9]  = probe.dominantDir.y;
    texels[10] = probe.dominantDir.z;
    texels[11] = probe.directionalRatio;

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

    // 2D texture layout: multiple probes per row to support millions of probes
    // Each probe uses 4 texels (pos+skyVis, ambient+sunVis, dominantDir+ratio, pointLight+intensity)
    // Width = PROBES_PER_ROW * 4, Height = ceil(probeCount / PROBES_PER_ROW)
    // Max capacity: 256 * 16384 = 4,194,304 probes
    const u32 MAX_TEXTURE_DIM = 16384;
    const u32 MAX_PROBES = PROBES_PER_ROW * MAX_TEXTURE_DIM;

    if (probeCount > MAX_PROBES)
    {
        Msg("! [LightProbeGrid] WARNING: Capping probes from %d to %d", probeCount, MAX_PROBES);
        probeCount = MAX_PROBES;
    }

    u32 texWidth = PROBES_PER_ROW * 4;  // 1024 texels wide (4 texels per probe)
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

    // Upload from persistent GPU cache (pre-built by WriteProbeToCache)
    if (m_gpuBufferDirty && m_pProbeTexture && !m_gpuCache.empty())
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(HW.pContext->Map(m_pProbeTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            if (mapped.RowPitch == m_gpuCacheRowPitch)
            {
                // Row pitch matches — single contiguous memcpy
                memcpy(mapped.pData, m_gpuCache.data(), m_gpuCacheRowPitch * texHeight);
            }
            else
            {
                // Row pitch differs — copy row by row
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

Fvector CLightProbeGrid::GetHashMin() const
{
    return m_hashMin;
}

Ivector CLightProbeGrid::GetHashDimensions() const
{
    return m_hashDims;
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

    m_hashDirty = true;
}

void CLightProbeGrid::PrepareHashGPUBuffer()
{
    if (m_spatialHash.empty()) return;

    u32 totalCells = (u32)m_spatialHash.size();

    // Texture layout: 1 texel per cell, 4 u32 probe indices per texel
    // Format: R32G32B32A32_UINT (16 bytes per texel)
    u32 texWidth = totalCells;
    u32 texHeight = 1;

    // Wrap into 2D if needed (max texture width 16384)
    const u32 MAX_WIDTH = 16384;
    if (texWidth > MAX_WIDTH)
    {
        texHeight = (texWidth + MAX_WIDTH - 1) / MAX_WIDTH;
        texWidth = MAX_WIDTH;
    }

    Msg("* [LightProbeGrid] Hash texture: %dx%d (%d cells, %.1f MB)",
        texWidth, texHeight, totalCells,
        (float)(texWidth * texHeight * 16) / (1024.0f * 1024.0f));

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
        texDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage = D3D11_USAGE_DYNAMIC;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = HW.pDevice->CreateTexture2D(&texDesc, nullptr, &m_pHashTexture);
        if (FAILED(hr))
        {
            Msg("! [LightProbeGrid] Hash texture creation failed: 0x%08X (size: %dx%d)", hr, texWidth, texHeight);
            return;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        R_CHK(HW.pDevice->CreateShaderResourceView(m_pHashTexture, &srvDesc, &m_pHashSRV));
    }

    // Upload data — 1 texel per cell, 4 x u32 indices
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(HW.pContext->Map(m_pHashTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        for (u32 c = 0; c < totalCells; c++)
        {
            const SpatialHashCell& cell = m_spatialHash[c];

            u32 row = c / texWidth;
            u32 col = c % texWidth;

            u32* rowPtr = (u32*)((u8*)mapped.pData + row * mapped.RowPitch);

            // 4 u32 per texel (RGBA32_UINT)
            rowPtr[col * 4 + 0] = cell.probeIndices[0];
            rowPtr[col * 4 + 1] = cell.probeIndices[1];
            rowPtr[col * 4 + 2] = cell.probeIndices[2];
            rowPtr[col * 4 + 3] = cell.probeIndices[3];
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
            neighbors.indices[n] = 0xFFFFFFFF;
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
                        neighbors.indices[n] = j;
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

    u32 probeCount = (u32)m_probes.size();

    // Ensure persistent buffers are sized (handles first call from Build before explicit resize)
    if (m_propagationBuffer.size() < probeCount)
        m_propagationBuffer.resize(probeCount);

    // Gather active probe indices within maxDist + 20m margin
    // (+20m prevents visible seams at the distance boundary)
    float maxDist = ps_r_probe_max_distance + 20.0f;
    float maxDistSq = maxDist * maxDist;
    Fvector playerPos = Device.vCameraPosition;

    m_propagationActiveSet.clear();
    for (u32 i = 0; i < probeCount; i++)
    {
        if (playerPos.distance_to_sqr(m_probes[i].position) < maxDistSq)
            m_propagationActiveSet.push_back(i);
    }

    u32 activeCount = (u32)m_propagationActiveSet.size();
    if (activeCount == 0) return;

    for (int iter = 0; iter < iterations; iter++)
    {
        // Compute new ambient for active probes only
        for (u32 a = 0; a < activeCount; a++)
        {
            u32 i = m_propagationActiveSet[a];
            const ProbeNeighbors& neighbors = m_probeNeighbors[i];
            Fvector neighborContrib = { 0, 0, 0 };
            float totalWeight = 0;

            for (int n = 0; n < 6; n++)
            {
                if (neighbors.indices[n] == 0xFFFFFFFF) continue;

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
                m_propagationBuffer[i].lerp(m_probes[i].ambient, neighborContrib, 0.15f);
            }
            else
            {
                m_propagationBuffer[i] = m_probes[i].ambient;
            }
        }

        // Copy back + update GPU cache for active probes only
        for (u32 a = 0; a < activeCount; a++)
        {
            u32 i = m_propagationActiveSet[a];
            m_probes[i].ambient = m_propagationBuffer[i];
        }
    }

    // Update GPU cache for all active probes
    for (u32 a = 0; a < activeCount; a++)
        WriteProbeToCache(m_propagationActiveSet[a]);

    m_gpuBufferDirty = true;
}
