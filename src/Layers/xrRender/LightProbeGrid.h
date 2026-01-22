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
    void BindToShader(u32 slot);    // Bind SRV to shader slot

    // Hybrid integration for CROS_impl
    bool SampleNearest(const Fvector& position, Fvector& outAmbient, float& outSkyVis);

    // Accessors
    u32  GetProbeCount() const { return (u32)m_probes.size(); }
    float GetLastUpdateTimeMs() const { return m_lastUpdateTimeMs; }
    bool IsDebugEnabled() const { return m_debugEnabled; }
    ID3D11Texture2D* GetTexture() const { return m_pProbeTexture; }  // For X-Ray texture binding

    // Grid bounds for shader constants
    Fvector GetBoundsMin() const;
    Fvector GetBoundsMax() const;
    Ivector GetDimensions() const;

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

    // Internal methods
    void PlaceProbesInSector(CSector* sector, u32 sectorIndex, bool isIndoor);
    void PlacePortalBridgeProbes(CPortal* portal);
    bool IsValidProbePosition(const Fvector& pos);
    bool IsSectorIndoor(CSector* sector);
    void UpdateProbe(CLightProbe& probe);
    void CastBounceRay(const Fvector& hitPos, const Fvector& hitNormal, Fvector& bounceAccum, const Fvector& sunDir, const Fvector& sunColor);
    Fvector ComputeTriangleNormal(const CDB::RESULT& hit);
    void BuildVisibleSectorSet();
    bool IsProbeInVisibleSector(const CLightProbe& probe);
    void ComputeGridBounds();
};

// Global instance pointer (set during level load)
extern CLightProbeGrid* g_LightProbeGrid;
