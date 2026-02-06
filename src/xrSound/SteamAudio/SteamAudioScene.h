#pragma once

#include <phonon.h>
#include "../../xrCDB/xrCDB.h"

class CSteamAudioSimulator;

/**
 * CSteamAudioScene - Manages Steam Audio scene geometry.
 *
 * Converts X-Ray CDB::MODEL collision geometry to Steam Audio's
 * IPLStaticMesh format for ray-traced audio simulation.
 *
 * Key responsibilities:
 * - Convert CDB::MODEL vertices/triangles to Steam Audio format
 * - Map X-Ray material IDs to acoustic properties
 * - Create and manage IPLSimulator for audio simulation
 * - Handle geometry updates when level changes
 */
class CSteamAudioScene
{
public:
    CSteamAudioScene();
    ~CSteamAudioScene();

    // Build scene from X-Ray collision model
    // Call this when level geometry is loaded (from set_geometry_occ)
    bool BuildFromCDBModel(CDB::MODEL* model);

    // Clear all geometry (call before level unload)
    void Clear();

    // Check if scene is ready for simulation
    bool IsReady() const { return m_scene != nullptr && m_simulator != nullptr; }

    // Accessors for simulation
    IPLScene GetScene() const { return m_scene; }
    IPLSimulator GetSimulator() const { return m_simulator; }

    // Batched commit mechanism - mark dirty, then flush once per frame
    // This is more efficient than committing after every source add/remove
    void MarkPendingCommit() { m_pendingCommit = true; }
    void FlushCommit();

    // Set the simulator wrapper for mutex-protected commits
    void SetSimulatorWrapper(CSteamAudioSimulator* sim) { m_simulatorWrapper = sim; }

    // Update listener position for simulation
    void SetListenerPosition(const Fvector& pos, const Fvector& dir, const Fvector& up);

    // Get geometry stats
    int GetVertexCount() const { return (int)m_vertices.size(); }
    int GetTriangleCount() const { return (int)m_triangles.size(); }

private:
    // Create Steam Audio objects
    bool CreateScene();
    bool CreateSimulator();
    bool CreateStaticMesh();

    // Destroy Steam Audio objects
    void DestroyStaticMesh();
    void DestroySimulator();
    void DestroyScene();

    // Steam Audio objects
    IPLScene m_scene = nullptr;
    IPLStaticMesh m_staticMesh = nullptr;
    IPLSimulator m_simulator = nullptr;

    // Geometry storage (converted from CDB)
    xr_vector<IPLVector3> m_vertices;
    xr_vector<IPLTriangle> m_triangles;
    xr_vector<IPLint32> m_materialIndices;
    xr_vector<IPLMaterial> m_materials;

    // Cached listener state
    IPLCoordinateSpace3 m_listenerCoords = {};

    // Batched commit flag - set by MarkPendingCommit(), cleared by FlushCommit()
    bool m_pendingCommit = false;

    // Deferred commit counter - after threshold, force a blocking commit
    // to prevent perpetual starvation from simulation threads holding shared locks
    int m_deferredCommitCount = 0;
    static constexpr int MAX_DEFERRED_COMMITS = 120;  // ~2 seconds at 60fps

    // Simulator wrapper for mutex-protected commits (set after simulator starts)
    CSteamAudioSimulator* m_simulatorWrapper = nullptr;
};
