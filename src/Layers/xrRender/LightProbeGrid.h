#pragma once

#include "../../xrCDB/xrCDB.h"
#include <set>

// Forward declarations
class CSector;
class CPortal;

//////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////
static const float OUTDOOR_GRID_SPACING = 3.0f;     // meters between probes outdoors
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
static const float RECEIVED_LIGHT_WEIGHT = 0.7f;    // how much received light affects sunVisibility

// Spatial hash constants
static const int   MAX_PROBES_PER_CELL = 8;         // max probes stored per hash cell
static const float DEFAULT_HASH_CELL_SIZE = 2.0f;   // spatial hash cell size (finer than probe spacing)
static const int   DEFAULT_PROPAGATION_ITERS = 2;   // light propagation iterations
static const int   DEFAULT_PROPAGATION_RATE = 30;   // frames between propagation passes

//////////////////////////////////////////////////////////////////////////
// GPU-compatible probe data structure (32 bytes, must match HLSL)
//////////////////////////////////////////////////////////////////////////
struct GPUProbeData
{
    Fvector3 position;      // 12 bytes
    float    skyVisibility; // 4 bytes
    Fvector3 ambient;       // 12 bytes
    float    sunVisibility; // 4 bytes
};                          // Total: 32 bytes

//////////////////////////////////////////////////////////////////////////
// CPU-side probe data with additional tracking info
//////////////////////////////////////////////////////////////////////////
struct CLightProbe
{
    Fvector3 position;       // World-space position
    float    skyVisibility;  // 0-1 sky visibility fraction
    Fvector3 ambient;        // Accumulated ambient (includes bounce)
    float    sunVisibility;  // 0-1 direct sun visibility
    Fvector3 bounce;         // Indirect sun contribution (debug)
    u16      sectorId;       // For update prioritization
    u16      lastUpdateFrame; // Frame counter for staggering
};

//////////////////////////////////////////////////////////////////////////
// Spatial hash cell - stores indices of probes within each cell
//////////////////////////////////////////////////////////////////////////
struct SpatialHashCell
{
    u16 probeIndices[MAX_PROBES_PER_CELL];  // Probe indices in this cell
    u8  count;                               // Number of valid entries
    u8  _pad;                                // Padding for alignment
};

//////////////////////////////////////////////////////////////////////////
// Probe neighbor connectivity - for light propagation
//////////////////////////////////////////////////////////////////////////
struct ProbeNeighbors
{
    u16   indices[6];    // Neighbor probe indices: +X, -X, +Y, -Y, +Z, -Z (0xFFFF = none)
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
    std::set<u16>           m_visibleSectors;

    // GPU resources (Texture2D: width=2, height=probeCount, RGBA32F)
    // Texel (x,0): position.xyz, skyVisibility
    // Texel (x,1): ambient.xyz, sunVisibility
    ID3D11Texture2D*          m_pProbeTexture;
    ID3D11ShaderResourceView* m_pProbeSRV;
    bool  m_gpuBufferDirty;
    u32   m_gpuTextureHeight;  // Current texture height (probe count)

    // Update state
    CDB::COLLIDER m_collider;    // Own instance for thread safety
    u32   m_updateBudget;
    u32   m_nextProbeIndex;
    u32   m_currentFrame;
    float m_bounceIntensity;
    float m_lastUpdateTimeMs;
    bool  m_debugEnabled;

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
    int   m_propagationIters;
    int   m_propagationRate;

    // Internal methods
    void PlaceProbesInSector(CSector* sector, u32 sectorIndex, bool isIndoor);
    void PlacePortalBridgeProbes(CPortal* portal);
    bool IsValidProbePosition(const Fvector& pos);
    bool IsSectorIndoor(CSector* sector);
    void UpdateProbe(CLightProbe& probe, u32 probeIndex);
    void CastBounceRay(const Fvector& hitPos, const Fvector& hitNormal, Fvector& bounceAccum, const Fvector& sunDir, const Fvector& sunColor);
    Fvector ComputeTriangleNormal(const CDB::RESULT& hit);
    void BuildVisibleSectorSet();
    bool IsProbeInVisibleSector(const CLightProbe& probe);
    void ComputeGridBounds();

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
