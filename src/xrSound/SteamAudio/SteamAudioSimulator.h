#pragma once

#include <phonon.h>
#include <atomic>
#include <thread>
#include <shared_mutex>

class CSteamAudioScene;

/**
 * CSteamAudioSimulator - Manages threaded audio simulation.
 *
 * Runs Steam Audio simulation on a dedicated thread to avoid
 * blocking the main game loop or audio thread.
 *
 * Two simulation types:
 * - Direct simulation: Occlusion, transmission (fast, runs every frame)
 * - Reflections simulation: Reverb (slow, runs less frequently)
 */
class CSteamAudioSimulator
{
public:
    CSteamAudioSimulator();
    ~CSteamAudioSimulator();

    // Start simulation threads with given scene
    bool Start(CSteamAudioScene* scene);

    // Stop simulation threads
    void Stop();

    // Check if running
    bool IsRunning() const { return m_running.load(); }

    // Request a new direct simulation (call from main thread after updating sources)
    void RequestDirectSimulation();

    // Check if direct simulation results are ready
    bool IsDirectResultReady() const { return m_directResultReady.load(); }

    // Consume the direct result (call from main thread after reading outputs)
    void ConsumeDirectResult() { m_directResultReady.store(false); }

    // Request a new reflections simulation
    void RequestReflectionsSimulation();

    // Check if reflections simulation results are ready
    bool IsReflectionsResultReady() const { return m_reflectionsResultReady.load(); }

    // Consume the reflections result
    void ConsumeReflectionsResult() { m_reflectionsResultReady.store(false); }

    // Safe commit methods — route ALL iplSimulatorCommit calls through these
    // to avoid racing with simulation threads.
    bool TryCommit();       // Non-blocking: returns false if simulation running
    void BlockingCommit();  // Blocking: waits for simulation to finish (shutdown only)

private:
    void DirectSimulationThread();
    void ReflectionsSimulationThread();

    CSteamAudioScene* m_scene = nullptr;
    IPLSimulator m_simulator = nullptr;

    // Direct simulation thread (occlusion, ~every frame)
    std::thread m_directThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_directRequestPending{false};
    std::atomic<bool> m_directResultReady{false};

    // Reflections simulation thread (reverb, ~100-200ms)
    std::thread m_reflectionsThread;
    std::atomic<bool> m_reflectionsRequestPending{false};
    std::atomic<bool> m_reflectionsResultReady{false};

    // Debug counter — resets on Start()
    int m_reflectionRunCount = 0;

    // Reader-writer lock: RunDirect/RunReflections take shared (can overlap),
    // Commit takes exclusive (waits for both to finish).
    std::shared_mutex m_simulationMutex;
};
