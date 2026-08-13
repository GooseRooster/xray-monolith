#include "../stdafx.h"
#include "SteamAudioSimulator.h"
#include "SteamAudioScene.h"

extern int g_SA_DebugLogging;

CSteamAudioSimulator::CSteamAudioSimulator()
{
}

CSteamAudioSimulator::~CSteamAudioSimulator()
{
    Stop();
}

bool CSteamAudioSimulator::Start(CSteamAudioScene* scene)
{
    if (!scene || !scene->IsReady())
    {
        Msg("! STEAM_AUDIO: Cannot start simulator - scene not ready");
        return false;
    }

    if (m_running.load())
    {
        Msg("! STEAM_AUDIO: Simulator already running");
        return false;
    }

    m_scene = scene;
    m_simulator = scene->GetSimulator();

    if (!m_simulator)
    {
        Msg("! STEAM_AUDIO: Cannot start simulator - null simulator");
        return false;
    }

    m_running.store(true);
    m_directRequestPending.store(false);
    m_directResultReady.store(false);
    m_reflectionsRequestPending.store(false);
    m_reflectionsResultReady.store(false);
    m_reflectionRunCount = 0;

    // Start direct simulation thread
    m_directThread = std::thread(&CSteamAudioSimulator::DirectSimulationThread, this);

    // Start reflections simulation thread
    m_reflectionsThread = std::thread(&CSteamAudioSimulator::ReflectionsSimulationThread, this);

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Simulation threads started");
    return true;
}

void CSteamAudioSimulator::Stop()
{
    if (!m_running.load())
        return;

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Stopping simulation threads...");

    m_running.store(false);

    // Wake up threads if they're waiting
    m_directRequestPending.store(true);
    m_reflectionsRequestPending.store(true);

    // Wait for threads to finish
    if (m_directThread.joinable())
        m_directThread.join();

    if (m_reflectionsThread.joinable())
        m_reflectionsThread.join();

    m_scene = nullptr;
    m_simulator = nullptr;

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Simulation threads stopped");
}

void CSteamAudioSimulator::RequestDirectSimulation()
{
    if (m_running.load())
    {
        m_directRequestPending.store(true);
    }
}

void CSteamAudioSimulator::RequestReflectionsSimulation()
{
    if (m_running.load())
    {
        m_reflectionsRequestPending.store(true);
    }
}

void CSteamAudioSimulator::DirectSimulationThread()
{
    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Direct simulation thread started");

    while (m_running.load())
    {
        // Wait for a simulation request
        if (m_directRequestPending.exchange(false))
        {
            if (!m_running.load())
                break;

            // Run direct simulation (occlusion, transmission, air absorption)
            // This ray-traces from listener to each source
            // Shared lock allows RunDirect and RunReflections to overlap,
            // but prevents concurrent iplSimulatorCommit (which takes exclusive lock)
            {
                std::shared_lock<std::shared_mutex> lock(m_simulationMutex);
                iplSimulatorRunDirect(m_simulator);
            }

            // Signal that results are ready
            m_directResultReady.store(true);
        }
        else
        {
            // Sleep briefly to avoid spinning
            // Using 1ms sleep - direct simulation should run nearly every frame
            Sleep(1);
        }
    }

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Direct simulation thread exiting");
}

void CSteamAudioSimulator::ReflectionsSimulationThread()
{
    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Reflections simulation thread started");

    while (m_running.load())
    {
        // Wait for a simulation request
        if (m_reflectionsRequestPending.exchange(false))
        {
            if (!m_running.load())
                break;

            // Run reflections simulation (reverb)
            // This ray-traces from listener into the scene to compute room response
            // This is more expensive than direct simulation
            // Shared lock allows RunDirect and RunReflections to overlap,
            // but prevents concurrent iplSimulatorCommit (which takes exclusive lock)
            {
                std::shared_lock<std::shared_mutex> lock(m_simulationMutex);
                iplSimulatorRunReflections(m_simulator);
            }

            // Signal that results are ready
            m_reflectionsResultReady.store(true);

            m_reflectionRunCount++;
            if (g_SA_DebugLogging && (m_reflectionRunCount % 10 == 0))
            {
                Msg("STEAM_AUDIO: Reflections simulation completed (run #%d)", m_reflectionRunCount);
            }
        }
        else
        {
            // Sleep longer - reflections don't need to update every frame
            // 10ms sleep gives ~100Hz max update rate, but we'll request less often
            Sleep(10);
        }
    }

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Reflections simulation thread exiting");
}

bool CSteamAudioSimulator::TryCommit()
{
    if (!m_simulator)
        return false;

    // Exclusive lock — waits for all shared locks (RunDirect/RunReflections) to release.
    // try_to_lock: returns immediately if any simulation is running.
    std::unique_lock<std::shared_mutex> lock(m_simulationMutex, std::try_to_lock);
    if (!lock.owns_lock())
    {
        if (g_SA_DebugLogging)
            Msg("STEAM_AUDIO: TryCommit deferred (simulation running)");
        return false;  // Simulation running, defer to next frame
    }

    iplSimulatorCommit(m_simulator);
    return true;
}

void CSteamAudioSimulator::BlockingCommit()
{
    if (!m_simulator)
        return;

    // Exclusive lock — blocks until all simulations finish (for shutdown path)
    std::unique_lock<std::shared_mutex> lock(m_simulationMutex);
    iplSimulatorCommit(m_simulator);
}
