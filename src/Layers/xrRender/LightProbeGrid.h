#pragma once

#include "../../xrCDB/xrCDB.h"
#include "../../xrCDB/Frustum.h"

// Forward declarations
class CPortal;
class ISpatial;

//////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////
static const float OUTDOOR_GRID_SPACING = 2.0f;     // meters between probes outdoors
static const float INDOOR_GRID_SPACING = 1.5f;      // meters between probes indoors
static const u32   PROBES_PER_ROW = 256;            // 2D texture layout: probes per texture row
static const float PORTAL_BRIDGE_OFFSET = 0.5f;     // offset from portal plane
static const float RAY_MAX_DISTANCE = 100.0f;       // max ray distance for sky test
static const int   RAYS_PER_PROBE = 8;              // Fibonacci hemisphere rays
// Default albedo — used when material lookup fails
// 0.35 approximates the Zone's predominantly dirty/weathered surfaces
static const float DEFAULT_ALBEDO = 0.35f;
static const float DEFAULT_BOUNCE_INTENSITY = 0.3f; // indirect sun multiplier

// Soft shadow and received light constants
static const int   SOFT_SHADOW_RAYS = 6;            // rays for soft sun shadows
static const float SOFT_SHADOW_JITTER = 0.15f;      // jitter cone angle (radians, ~8.5 degrees)
static const float SUN_ALIGNMENT_THRESHOLD = 0.5f;  // cos(60°) - rays within 60° of sun count
static const float RECEIVED_LIGHT_WEIGHT = 0.35f;    // how much received light affects sunVisibility

// Spatial hash constants (CPU-side only — GPU hash removed in favor of volume textures)
static const int   MAX_PROBES_PER_CELL = 4;         // max probes stored per hash cell
static const float DEFAULT_HASH_CELL_SIZE = 2.5f;   // spatial hash cell size (just above outdoor spacing, reduces overflow)
static const float INDOOR_RAY_RANGE = 30.0f;         // upward ray range for indoor detection
static const int   INDOOR_RAY_COUNT = 5;             // number of upward rays for indoor detection
static const int   INDOOR_RAY_THRESHOLD = 4;         // hits needed to classify as indoor
static const int   DEFAULT_PROPAGATION_ITERS = 1;   // light propagation iterations per pass (amortized: 1 iter every 7 frames vs 3 every 20)
static const int   DEFAULT_PROPAGATION_RATE = 7;    // frames between propagation passes

// Point light injection constants
static const float POINT_LIGHT_SEARCH_RADIUS = 20.0f;  // max distance to query lights
static const int   MAX_POINT_LIGHTS_PER_PROBE = 5;     // cap per-probe to bound worst case
static const int   MAX_BOUNCE_LIGHTS_PER_RAY = 2;      // cap per bounce ray (controls CDB shadow queries)

// Volume texture constants
static const u32   MAX_VOLUME_VOXELS = 500000;       // cap total voxels (~24MB for 3 textures)
static const int   NUM_VOLUME_TEXTURES = 3;           // vol0=ambient+sky, vol1=shDirection+pad, vol2=ptlight+sun

// Probe update quality levels
enum EProbeQuality
{
    PROBE_QUALITY_FULL = 0,      // 8 hemisphere + 6 shadow + bounce rays (~30 CDB queries)
    PROBE_QUALITY_REDUCED = 1,   // 6 hemisphere + 3 shadow, no bounce (~11 CDB queries)
};

//////////////////////////////////////////////////////////////////////////
// CPU-side probe data with additional tracking info
// GPU layout (64 bytes per probe, 4 × RGBA32F texels):
//   Texel 0: position.xyz, skyVisibility
//   Texel 1: ambient.xyz, sunVisibility
//   Texel 2: shDirection.xyz, 0.0 (L1 SH directional vector)
//   Texel 3: pointLightColor.xyz, pointLightIntensity
//////////////////////////////////////////////////////////////////////////
struct CLightProbe
{
    Fvector3 position;           // World-space position
    float    skyVisibility;      // 0-1 sky visibility fraction
    Fvector3 ambient;            // Accumulated ambient (includes bounce)
    float    sunVisibility;      // 0-1 direct sun visibility
    Fvector3 bounce;             // Indirect sun contribution (debug)
    Fvector3 shDirection;        // L1 SH directional vector (unnormalized — magnitude = directional strength)
    float    _shPad;             // Padding (maintains 64-byte GPU layout)
    Fvector3 pointLightColor;    // Accumulated point/spot light color
    float    pointLightIntensity; // Point light luminance
    float    envLuminance;       // Environment luminance at last ray-update (for ToD snap)
    u16      sectorId;           // Reserved (0xFFFF) — preserves GPU layout
    u16      lastUpdateFrame;    // Frame counter for staggering
};

//////////////////////////////////////////////////////////////////////////
// Spatial hash cell - stores indices of probes within each cell
//////////////////////////////////////////////////////////////////////////
struct SpatialHashCell
{
    u32 probeIndices[MAX_PROBES_PER_CELL];  // Probe indices (u32 supports >65K probes)
    u8  count;                               // Number of valid entries
    u8  _pad[3];                             // Padding for alignment
};

//////////////////////////////////////////////////////////////////////////
// Probe neighbor connectivity - for light propagation
//////////////////////////////////////////////////////////////////////////
struct ProbeNeighbors
{
    u32   indices[6];    // Neighbor probe indices: +X, -X, +Y, -Y, +Z, -Z (0xFFFFFFFF = none)
    float distances[6];  // Distance to each neighbor
};

//////////////////////////////////////////////////////////////////////////
// VoxelAccum - Per-voxel accumulator for scatter-normalize rasterization
//////////////////////////////////////////////////////////////////////////
struct VoxelAccum
{
    float ambient[3];
    float skyVis;
    float sunVis;
    float shDir[3];              // L1 SH directional vector accumulator
    float _shPad;                // Unused (weight accumulator is separate)
    float pointLight[3];
    float weight;
};

//////////////////////////////////////////////////////////////////////////
// CLightProbeGrid - Main probe system class
//////////////////////////////////////////////////////////////////////////
class CLightProbeGrid
{
public:
    CLightProbeGrid();
    ~CLightProbeGrid();

    // Lifecycle
    void Build();                   // Called after level_Load
    void Clear();                   // Called in level_Unload
    void Update();                  // Called each frame from OnFrame
    void PrepareGPUBuffer();        // Upload probe data to GPU (probe data texture)
    void BindToShader(u32 slot);    // Bind probe SRV to shader slot

    // Hybrid integration for CROS_impl
    bool SampleNearest(const Fvector& position, Fvector& outAmbient, float& outSkyVis);

    // Accessors
    u32  GetProbeCount() const { return (u32)m_probes.size(); }
    float GetLastUpdateTimeMs() const { return m_lastUpdateTimeMs; }
    bool IsDebugEnabled() const { return m_debugEnabled; }
    ID3D11Texture2D* GetTexture() const { return m_pProbeTexture; }  // For X-Ray texture binding (debug viz)

    // Volume texture accessors (for render target binding)
    ID3D11Texture3D*          GetVolumeTexture(int idx) const;
    ID3D11ShaderResourceView* GetVolumeSRV(int idx) const;
    Fvector  GetVolumeMin() const  { return m_volMin; }
    Fvector  GetVolumeSize() const { return m_volSize; }
    float    GetVoxelSize() const  { return m_voxelSize; }

    // Grid bounds for shader constants
    Fvector GetBoundsMin() const;
    Fvector GetBoundsMax() const;
    Ivector GetDimensions() const;

private:
    // Probe storage
    xr_vector<CLightProbe>  m_probes;

    // GPU resources — Probe data texture (Texture2D: PROBES_PER_ROW*4 wide, RGBA32F)
    // Kept for debug visualization only (LoadProbe in shader, debug modes 1-8).
    // Upload skipped when ps_r_debug_probes == 0.
    ID3D11Texture2D*          m_pProbeTexture;
    ID3D11ShaderResourceView* m_pProbeSRV;
    bool  m_gpuBufferDirty;
    u32   m_gpuAllocatedProbes;  // Probe count at last texture allocation (grow-on-demand)

    // Persistent GPU cache — pre-built texel buffer (avoids per-probe conversion each upload)
    xr_vector<u8> m_gpuCache;        // texWidth × texHeight × 16 bytes
    u32 m_gpuCacheRowPitch;           // Row pitch (texWidth_texels × 16 bytes)
    u32 m_gpuCacheTexHeight;          // Texture height (rows)

    // =========================================================================
    // Volume textures (Irradiance Volumes) — replaces GPU spatial hash
    // 3 × Texture3D<float4> with hardware trilinear filtering
    //   vol0: ambient.rgb, skyVisibility
    //   vol1: shDirection.xyz, 0.0 (L1 SH directional vector)
    //   vol2: pointLightColor.rgb, sunVisibility
    // =========================================================================
    ID3D11Texture3D*           m_pVolTexture[NUM_VOLUME_TEXTURES];
    ID3D11ShaderResourceView*  m_pVolSRV[NUM_VOLUME_TEXTURES];
    Ivector  m_volDims;       // Volume dimensions in voxels
    Fvector  m_volMin;        // Volume world-space minimum
    Fvector  m_volSize;       // Volume world-space extent (max - min)
    float    m_voxelSize;     // Actual voxel size (may auto-coarsen)
    bool     m_volDirty;      // Needs re-rasterization

    // Rasterization accumulators and staging buffers
    xr_vector<VoxelAccum>  m_volAccum;
    xr_vector<float>       m_volData[NUM_VOLUME_TEXTURES];  // 3 × float4 staging buffers for upload

    // =========================================================================
    // Simplified 2-tier scheduling (replaces 4-tier distance-based)
    // =========================================================================
    xr_vector<u32>  m_frustumProbes;       // Rebuilt each frame: in-frustum probe indices
    u32             m_frustumRobinIndex;   // Round-robin cursor for in-frustum tier
    u32             m_bgRobinIndex;        // Round-robin cursor for background tier
    u32             m_budgetBoostFramesLeft; // Frames remaining with boosted budget

    // Update state
    CDB::COLLIDER m_collider;    // Own instance for thread safety
    u32   m_updateBudget;
    u32   m_currentFrame;
    float m_bounceIntensity;
    float m_lastUpdateTimeMs;
    bool  m_debugEnabled;
    u32   m_lastUploadFrame; // Frame of last GPU upload (for throttling)

    // Frustum and camera tracking
    CFrustum m_viewFrustum;      // Current frame camera frustum
    Fvector  m_prevCameraDir;    // Previous frame camera direction (rotation detection)

    // Time-of-day tracking
    float m_lastGameTime;        // Previous frame game time (for jump detection)
    float m_temporalBlend;       // Adaptive temporal smoothing factor (0.3 normal, up to 1.0 on time jump)
    float m_currentEnvLum;       // Current frame environment luminance

    // Grid bounds (computed during Build)
    Fvector m_boundsMin;
    Fvector m_boundsMax;
    Ivector m_gridDims;

    // Spatial hash acceleration structure (CPU-side only — used for UpdateProbesInRadius,
    // SampleNearest/CROS, HasNearbyProbe, BuildNeighborConnectivity)
    xr_vector<SpatialHashCell> m_spatialHash;
    Ivector m_hashDims;
    Fvector m_hashMin;
    float   m_hashCellSize;

    // Neighbor connectivity for light propagation
    xr_vector<ProbeNeighbors> m_probeNeighbors;

    // Per-material RGB albedo (indexed by CDB vector index)
    xr_vector<Fvector> m_materialAlbedos;

    // Reusable spatial query buffer for point light injection
    xr_vector<ISpatial*> m_lightQueryResults;

    // Cached lights for bounce computation (queried once per UpdateProbe, reused in CastBounceRay)
    struct CachedBounceLight {
        Fvector position;
        Fvector color;     // L->color as Fvector
        float   range;
        float   attenuation0, attenuation1, attenuation2;
    };
    xr_vector<CachedBounceLight> m_bounceLightCache;

    int   m_propagationIters;
    int   m_propagationRate;

    // Persistent propagation buffers (avoid per-call heap allocation)
    xr_vector<Fvector> m_propagationBuffer;    // Sized to m_probes.size() at Build()
    xr_vector<u32>     m_propagationActiveSet;  // Reusable active index list

    // Internal methods — placement
    void PlacePortalBridgeProbes(CPortal* portal);
    bool IsValidProbePosition(const Fvector& pos);
    bool IsPositionIndoor(const Fvector& pos);
    CLightProbe MakeDefaultProbe(const Fvector& pos);
    bool HasNearbyProbe(const Fvector& pos, float minDist) const;
    void ComputeGridBounds();

    // Internal methods — update
    void UpdateProbe(CLightProbe& probe, u32 probeIndex, EProbeQuality quality = PROBE_QUALITY_FULL);
    void WriteProbeToCache(u32 probeIndex);   // Write 64 bytes to persistent GPU cache
    void InitGPUCache();                       // Allocate cache and fill all entries
    void CastBounceRay(const Fvector& hitPos, const Fvector& hitNormal, Fvector& bounceAccum,
                       const Fvector& sunDir, const Fvector& sunColor,
                       const Fvector& skyColor, const Fvector& albedo,
                       float probeSkyVisibility);
    Fvector ComputeTriangleNormal(const CDB::RESULT& hit);
    float ComputeEnvLuminance() const;

    // Material albedo methods
    void BuildMaterialAlbedos();       // Parse GMLib at Build() time → m_materialAlbedos
    Fvector GetMaterialAlbedo(u16 materialIdx) const;  // Lookup by CDB vector index

    // Spatial hash methods (CPU-side only)
    void BuildSpatialHash();
    Ivector WorldToHashCell(const Fvector& pos) const;

    // Volume texture methods
    void BuildVolumeTextures();    // Create Texture3D resources during Build()
    void RasterizeVolume();        // Full scatter-normalize (Build + time jump only)
    void UpdateVolumeProbe(u32 probeIndex, const CLightProbe& oldValues); // Incremental update
    void NormalizeVoxel(int voxelIdx); // Re-normalize one voxel from accum → staging
    void PrepareVolumeGPU();       // Upload staging buffers to Texture3D (MAP_WRITE_DISCARD)
    void RebuildFrustumList();     // Classify probes as in/out of frustum

    // Neighbor connectivity and propagation
    void BuildNeighborConnectivity();
    void PropagateLight(int iterations);
};

// Global instance pointer (set during level load)
extern CLightProbeGrid* g_LightProbeGrid;
