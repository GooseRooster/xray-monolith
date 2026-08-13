#include "stdafx.h"
#include "SteamAudioScene.h"
#include "SteamAudio.h"
#include "SteamAudioMaterials.h"
#include "SteamAudioSimulator.h"

extern int g_SA_DebugLogging;
extern int psSA_ReverbRays;
extern int psSA_ReverbBounces;
extern u32 psSndQuality;

CSteamAudioScene::CSteamAudioScene()
{
}

CSteamAudioScene::~CSteamAudioScene()
{
    Clear();
}

bool CSteamAudioScene::BuildFromCDBModel(CDB::MODEL* model)
{
    if (!model)
    {
        Msg("! STEAM_AUDIO: Cannot build scene - null model");
        return false;
    }

    if (!CSteamAudio::Instance().IsAvailable())
    {
        Msg("! STEAM_AUDIO: Cannot build scene - Steam Audio not initialized");
        return false;
    }

    // Clear any existing geometry (also resets material config via Reset())
    Clear();

    // Load material configuration AFTER Clear() — Clear() calls Reset()
    // which wipes s_customMappings and s_configLoaded, so LoadConfig() must
    // run after to re-populate the mappings before MapMaterialId() is called.
    SteamAudioMaterials::LoadConfig();
    SteamAudioMaterials::ResetDiagnostics();

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Building scene from CDB model (%d vertices, %d triangles)",
            model->get_verts_count(), model->get_tris_count());

    // Get geometry from CDB model
    int vertCount = model->get_verts_count();
    int triCount = model->get_tris_count();
    Fvector* xrVerts = model->get_verts();
    CDB::TRI* xrTris = model->get_tris();

    if (vertCount == 0 || triCount == 0)
    {
        Msg("! STEAM_AUDIO: Cannot build scene - empty geometry");
        return false;
    }

    // Convert vertices
    // X-Ray: Left-handed coordinate system (+X right, +Y up, +Z forward)
    // Steam Audio: Right-handed coordinate system (+X right, +Y up, -Z forward)
    // To convert left-handed to right-handed: negate Z axis
    m_vertices.resize(vertCount);
    for (int i = 0; i < vertCount; i++)
    {
        m_vertices[i].x = xrVerts[i].x;
        m_vertices[i].y = xrVerts[i].y;
        m_vertices[i].z = -xrVerts[i].z;  // Negate Z for coordinate system conversion
    }

    // Convert triangles and extract material indices
    // NOTE: Swapping indices 1 and 2 reverses triangle winding to preserve
    // correct face normals after Z negation (handedness change)
    m_triangles.resize(triCount);
    m_materialIndices.resize(triCount);

    for (int i = 0; i < triCount; i++)
    {
        m_triangles[i].indices[0] = xrTris[i].verts[0];
        m_triangles[i].indices[1] = xrTris[i].verts[2];  // Swap 1 and 2 to reverse winding
        m_triangles[i].indices[2] = xrTris[i].verts[1];  // after Z negation

        // CDB stores vector index (translated from gamemtl ID by Level_load.cpp)
        u32 vecIdx = xrTris[i].material & 0x3FFF;
        m_materialIndices[i] = SteamAudioMaterials::MapMaterialId(vecIdx);
    }

    // Log which material IDs lack explicit mappings — helps populate steam_audio_materials.ltx
    SteamAudioMaterials::LogUnmappedSummary();

    // Get material presets
    m_materials = SteamAudioMaterials::GetMaterialPresets();

    // Create Steam Audio objects
    if (!CreateScene())
        return false;

    if (!CreateStaticMesh())
    {
        DestroyScene();
        return false;
    }

    if (!CreateSimulator())
    {
        DestroyStaticMesh();
        DestroyScene();
        return false;
    }

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Scene built successfully (%d verts, %d tris, %d materials)",
            (int)m_vertices.size(), (int)m_triangles.size(), (int)m_materials.size());

    return true;
}

void CSteamAudioScene::Clear()
{
    m_simulatorWrapper = nullptr;

    DestroySimulator();
    DestroyStaticMesh();
    DestroyScene();

    // Reset material config so it re-reads from file on next level load
    SteamAudioMaterials::Reset();

    m_vertices.clear();
    m_triangles.clear();
    m_materialIndices.clear();
    m_materials.clear();
}

bool CSteamAudioScene::CreateScene()
{
    IPLContext context = CSteamAudio::Instance().GetContext();
    if (!context)
        return false;

    IPLSceneSettings sceneSettings = {};
    sceneSettings.type = IPL_SCENETYPE_DEFAULT;  // Built-in ray tracer

    IPLerror error = iplSceneCreate(context, &sceneSettings, &m_scene);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create scene (error: %d)", error);
        return false;
    }

    return true;
}

bool CSteamAudioScene::CreateStaticMesh()
{
    if (!m_scene)
        return false;

    IPLStaticMeshSettings meshSettings = {};
    meshSettings.numVertices = (IPLint32)m_vertices.size();
    meshSettings.numTriangles = (IPLint32)m_triangles.size();
    meshSettings.numMaterials = (IPLint32)m_materials.size();
    meshSettings.vertices = m_vertices.data();
    meshSettings.triangles = m_triangles.data();
    meshSettings.materialIndices = m_materialIndices.data();
    meshSettings.materials = m_materials.data();

    IPLerror error = iplStaticMeshCreate(m_scene, &meshSettings, &m_staticMesh);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create static mesh (error: %d)", error);
        return false;
    }

    // Add mesh to scene and commit
    iplStaticMeshAdd(m_staticMesh, m_scene);
    iplSceneCommit(m_scene);

    return true;
}

bool CSteamAudioScene::CreateSimulator()
{
    IPLContext context = CSteamAudio::Instance().GetContext();
    if (!context || !m_scene)
        return false;

    const IPLAudioSettings& audioSettings = CSteamAudio::Instance().GetAudioSettings();

    IPLSimulationSettings simSettings = {};
    simSettings.flags = (IPLSimulationFlags)(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
    simSettings.sceneType = IPL_SCENETYPE_DEFAULT;

    // Direct simulation (occlusion) settings
    // Must match upper bound of psSA_OcclusionRays console var (1-32)
    simSettings.maxNumOcclusionSamples = 32;

    // Reflection simulation (reverb) settings
    simSettings.maxNumRays = 4096;
    simSettings.numDiffuseSamples = 32;
    simSettings.maxDuration = 2.0f;  // Max reverb tail in seconds
    simSettings.maxNumSources = 256;  // Max sources in simulator (per-source direct + 1 listener reverb probe)
    // Note: numBounces is set per-frame in IPLSimulationSharedInputs, not here

    // Always HYBRID: convolution early reflections + parametric FDN late tail
    simSettings.reflectionType = IPL_REFLECTIONEFFECTTYPE_HYBRID;
    simSettings.maxOrder = 2;   // 2nd-order ambisonics (9ch)

    // Threading
    simSettings.numThreads = 4;
    simSettings.rayBatchSize = 32;

    // Visibility samples (required even if not using pathing)
    simSettings.numVisSamples = 16;

    // Audio format (match X-Ray engine)
    simSettings.samplingRate = audioSettings.samplingRate;
    simSettings.frameSize = audioSettings.frameSize;

    IPLerror error = iplSimulatorCreate(context, &simSettings, &m_simulator);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create simulator (error: %d)", error);
        return false;
    }

    // Connect scene to simulator
    iplSimulatorSetScene(m_simulator, m_scene);
    iplSimulatorCommit(m_simulator);

    return true;
}

void CSteamAudioScene::DestroyScene()
{
    if (m_scene)
    {
        iplSceneRelease(&m_scene);
        m_scene = nullptr;
    }
}

void CSteamAudioScene::DestroyStaticMesh()
{
    if (m_staticMesh)
    {
        if (m_scene)
            iplStaticMeshRemove(m_staticMesh, m_scene);
        iplStaticMeshRelease(&m_staticMesh);
        m_staticMesh = nullptr;
    }
}

void CSteamAudioScene::DestroySimulator()
{
    if (m_simulator)
    {
        iplSimulatorRelease(&m_simulator);
        m_simulator = nullptr;
    }
}

void CSteamAudioScene::FlushCommit()
{
    if (!m_pendingCommit || !m_simulator)
        return;

    if (m_simulatorWrapper)
    {
        // After too many deferred attempts, force a blocking commit.
        // TryCommit uses try_to_lock which fails if ANY simulation thread holds a shared lock.
        // With two threads sleeping 1ms/10ms between simulations, the window for exclusive
        // access is tiny. After ~2 seconds of starvation, blocking ensures sources get committed.
        if (m_deferredCommitCount >= MAX_DEFERRED_COMMITS)
        {
            m_simulatorWrapper->BlockingCommit();
            m_pendingCommit = false;
            if (g_SA_DebugLogging)
                Msg("STEAM_AUDIO: Forced blocking commit after %d deferrals", m_deferredCommitCount);
            m_deferredCommitCount = 0;
        }
        else if (m_simulatorWrapper->TryCommit())
        {
            m_pendingCommit = false;
            m_deferredCommitCount = 0;
        }
        else
        {
            m_deferredCommitCount++;
            if (g_SA_DebugLogging && (m_deferredCommitCount % 60 == 0))
                Msg("STEAM_AUDIO: TryCommit deferred (simulation running) [%d]", m_deferredCommitCount);
        }
    }
    else
    {
        // Fallback: direct commit (only during init before wrapper is set)
        iplSimulatorCommit(m_simulator);
        m_pendingCommit = false;
        m_deferredCommitCount = 0;
    }
}

void CSteamAudioScene::SetListenerPosition(const Fvector& pos, const Fvector& dir, const Fvector& up)
{
    if (!m_simulator)
        return;

    // Convert X-Ray left-handed to Steam Audio right-handed: negate Z
    // Set listener coordinate system
    m_listenerCoords.origin.x = pos.x;
    m_listenerCoords.origin.y = pos.y;
    m_listenerCoords.origin.z = -pos.z;  // Negate Z

    // Direction vectors also need Z negation
    m_listenerCoords.ahead.x = dir.x;
    m_listenerCoords.ahead.y = dir.y;
    m_listenerCoords.ahead.z = -dir.z;  // Negate Z

    m_listenerCoords.up.x = up.x;
    m_listenerCoords.up.y = up.y;
    m_listenerCoords.up.z = -up.z;  // Negate Z

    // Compute right vector from cross product of up and ahead (in right-handed system)
    // After Z negation: right = up_converted x ahead_converted
    float ahead_z = -dir.z;
    float up_z = -up.z;
    m_listenerCoords.right.x = up.y * ahead_z - up_z * dir.y;
    m_listenerCoords.right.y = up_z * dir.x - up.x * ahead_z;
    m_listenerCoords.right.z = up.x * dir.y - up.y * dir.x;

    // Set shared inputs for simulation
    IPLSimulationSharedInputs sharedInputs = {};
    sharedInputs.listener = m_listenerCoords;
    sharedInputs.numRays = psSA_ReverbRays;
    sharedInputs.numBounces = psSA_ReverbBounces;
    sharedInputs.duration = 2.0f;  // Always full propagation for accurate RT60/EQ estimation
    sharedInputs.order = 2;
    sharedInputs.irradianceMinDistance = 1.0f;

    iplSimulatorSetSharedInputs(m_simulator, (IPLSimulationFlags)(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS), &sharedInputs);
}
