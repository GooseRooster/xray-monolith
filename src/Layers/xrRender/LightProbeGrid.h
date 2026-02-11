#pragma once

#include "../../xrCDB/xrCDB.h"

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
static const int   RAYS_PER_PROBE = 6;              // Fibonacci hemisphere rays
static const float ASSUMED_ALBEDO = 0.5f;           // neutral gray for bounce
static const float DEFAULT_BOUNCE_INTENSITY = 0.3f; // indirect sun multiplier

// Soft shadow and received light constants
static const int   SOFT_SHADOW_RAYS = 4;            // rays for soft sun shadows
static const float SOFT_SHADOW_JITTER = 0.15f;      // jitter cone angle (radians, ~8.5 degrees)
static const float SUN_ALIGNMENT_THRESHOLD = 0.5f;  // cos(60°) - rays within 60° of sun count
static const float RECEIVED_LIGHT_WEIGHT = 0.35f;    // how much received light affects sunVisibility

// Spatial hash constants
static const int   MAX_PROBES_PER_CELL = 4;         // max probes stored per hash cell (1 texel @ R32G32B32A32_UINT)
static const float DEFAULT_HASH_CELL_SIZE = 4.0f;   // spatial hash cell size (2x outdoor spacing)
static const float INDOOR_RAY_RANGE = 30.0f;         // upward ray range for indoor detection
static const int   INDOOR_RAY_COUNT = 5;             // number of upward rays for indoor detection
static const int   INDOOR_RAY_THRESHOLD = 4;         // hits needed to classify as indoor
static const int   DEFAULT_PROPAGATION_ITERS = 2;   // light propagation iterations
static const int   DEFAULT_PROPAGATION_RATE = 30;   // frames between propagation passes

// Point light injection constants
static const float POINT_LIGHT_SEARCH_RADIUS = 20.0f;  // max distance to query lights
static const int   MAX_POINT_LIGHTS_PER_PROBE = 5;     // cap per-probe to bound worst case

//////////////////////////////////////////////////////////////////////////
// GPU-compatible probe data structure (64 bytes, must match HLSL)
//////////////////////////////////////////////////////////////////////////
struct GPUProbeData
{
    Fvector3 position;          // texel 0: xyz   (12 bytes)
    float    skyVisibility;     // texel 0: w     (4 bytes)
    Fvector3 ambient;           // texel 1: xyz   (12 bytes)
    float    sunVisibility;     // texel 1: w     (4 bytes)
    Fvector3 dominantDir;       // texel 2: xyz   (12 bytes)
    float    directionalRatio;  // texel 2: w     (4 bytes)
    Fvector3 pointLightColor;   // texel 3: xyz   (12 bytes)
    float    pointLightIntensity; // texel 3: w   (4 bytes)
};                              // Total: 64 bytes

//////////////////////////////////////////////////////////////////////////
// CPU-side probe data with additional tracking info
//////////////////////////////////////////////////////////////////////////
struct CLightProbe
{
    Fvector3 position;           // World-space position
    float    skyVisibility;      // 0-1 sky visibility fraction
    Fvector3 ambient;            // Accumulated ambient (includes bounce)
    float    sunVisibility;      // 0-1 direct sun visibility
    Fvector3 bounce;             // Indirect sun contribution (debug)
    Fvector3 dominantDir;        // Energy-weighted primary light direction
    float    directionalRatio;   // 0=uniform, 1=all from one direction
    Fvector3 pointLightColor;    // Accumulated point/spot light color
    float    pointLightIntensity; // Point light luminance
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
    void PrepareGPUBuffer();        // Upload probe data to GPU
    void BindToShader(u32 slot);    // Bind probe SRV to shader slot
    void BindHashToShader(u32 slot); // Bind spatial hash SRV to shader slot

    // Hybrid integration for CROS_impl
    bool SampleNearest(const Fvector& position, Fvector& outAmbient, float& outSkyVis);

    // Accessors
    u32  GetProbeCount() const { return (u32)m_probes.size(); }
    float GetLastUpdateTimeMs() const { return m_lastUpdateTimeMs; }
    bool IsDebugEnabled() const { return m_debugEnabled; }
    ID3D11Texture2D* GetTexture() const { return m_pProbeTexture; }  // For X-Ray texture binding
    ID3D11Texture2D* GetHashTexture() const { return m_pHashTexture; }  // For hash texture binding

    // Grid bounds for shader constants
    Fvector GetBoundsMin() const;
    Fvector GetBoundsMax() const;
    Ivector GetDimensions() const;

    // Spatial hash bounds for shader constants
    Fvector GetHashMin() const;
    Ivector GetHashDimensions() const;
    float   GetHashCellSize() const { return m_hashCellSize; }

private:
    // Probe storage
    xr_vector<CLightProbe>  m_probes;

    // GPU resources (Texture2D: PROBES_PER_ROW*4 wide, RGBA32F)
    // Texel 0: position.xyz, skyVisibility
    // Texel 1: ambient.xyz, sunVisibility
    // Texel 2: dominantDir.xyz, directionalRatio
    // Texel 3: pointLightColor.xyz, pointLightIntensity
    ID3D11Texture2D*          m_pProbeTexture;
    ID3D11ShaderResourceView* m_pProbeSRV;
    bool  m_gpuBufferDirty;
    u32   m_gpuTextureHeight;  // Current texture height (probe count)

    // Persistent GPU cache — pre-built texel buffer (avoids per-probe conversion each upload)
    xr_vector<u8> m_gpuCache;        // texWidth × texHeight × 16 bytes
    u32 m_gpuCacheRowPitch;           // Row pitch (texWidth_texels × 16 bytes)
    u32 m_gpuCacheTexHeight;          // Texture height (rows)

    // Update state
    CDB::COLLIDER m_collider;    // Own instance for thread safety
    u32   m_updateBudget;
    u32   m_currentFrame;
    float m_bounceIntensity;
    float m_lastUpdateTimeMs;
    bool  m_debugEnabled;
    u32   m_farRobinIndex;   // Round-robin index for far/distant tier updates
    u32   m_lastUploadFrame; // Frame of last GPU upload (for throttling)

    // Grid bounds (computed during Build)
    Fvector m_boundsMin;
    Fvector m_boundsMax;
    Ivector m_gridDims;

    // Spatial hash acceleration structure
    xr_vector<SpatialHashCell> m_spatialHash;
    Ivector m_hashDims;
    Fvector m_hashMin;
    float   m_hashCellSize;

    // Spatial hash GPU resources
    ID3D11Texture2D*          m_pHashTexture;
    ID3D11ShaderResourceView* m_pHashSRV;
    bool  m_hashDirty;

    // Neighbor connectivity for light propagation
    xr_vector<ProbeNeighbors> m_probeNeighbors;

    // Reusable spatial query buffer for point light injection
    xr_vector<ISpatial*> m_lightQueryResults;
    int   m_propagationIters;
    int   m_propagationRate;

    // Persistent propagation buffers (avoid per-call heap allocation)
    xr_vector<Fvector> m_propagationBuffer;    // Sized to m_probes.size() at Build()
    xr_vector<u32>     m_propagationActiveSet;  // Reusable active index list

    // Internal methods — placement
    void PlacePortalBridgeProbes(CPortal* portal);
    bool IsValidProbePosition(const Fvector& pos);
    bool IsPositionIndoor(const Fvector& pos);
    CLightProbe MakeDefaultProbe(const Fvector& pos, bool isIndoor);
    bool HasNearbyProbe(const Fvector& pos, float minDist) const;
    void ComputeGridBounds();

    // Internal methods — update
    void UpdateProbe(CLightProbe& probe, u32 probeIndex);
    u32  UpdateProbesInRadius(const Fvector& center, float minDist, float maxDist, u32 budget);
    void WriteProbeToCache(u32 probeIndex);   // Write 64 bytes to persistent GPU cache
    void InitGPUCache();                       // Allocate cache and fill all entries
    void CastBounceRay(const Fvector& hitPos, const Fvector& hitNormal, Fvector& bounceAccum, const Fvector& sunDir, const Fvector& sunColor);
    Fvector ComputeTriangleNormal(const CDB::RESULT& hit);

    // Spatial hash methods
    void BuildSpatialHash();
    void PrepareHashGPUBuffer();
    Ivector WorldToHashCell(const Fvector& pos) const;

    // Neighbor connectivity and propagation
    void BuildNeighborConnectivity();
    void PropagateLight(int iterations);
};

// Global instance pointer (set during level load)
extern CLightProbeGrid* g_LightProbeGrid;
