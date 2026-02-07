#pragma once

#include <phonon.h>

class CSteamAudioScene;

// Debug function to log source lifecycle stats - helps diagnose memory leaks
void SteamAudioSource_LogStats();

// Global debug logging flag - controlled via snd_sa_debug console command
extern int g_SA_DebugLogging;

/**
 * CSteamAudioSource - Per-emitter Steam Audio wrapper.
 *
 * Each 3D sound emitter that uses Steam Audio has one of these.
 * Manages:
 * - IPLSource for direct simulation (occlusion, transmission, air absorption)
 * - IPLDirectEffect for direct path processing
 * - Simulation inputs/outputs
 * - Audio buffer processing (mono in-place)
 *
 * Spatialization (HRTF/panning) is handled entirely by OpenAL.
 * Reverb is handled by EFX, with decay times driven by CSteamAudioReverb's listener probe.
 */
class CSteamAudioSource
{
public:
    CSteamAudioSource();
    ~CSteamAudioSource();

    // Initialize with simulator from scene
    bool Initialize(CSteamAudioScene* scene);
    void Destroy();

    bool IsInitialized() const { return m_source != nullptr; }

    // Update source position for next simulation
    // minDistance: radius (meters) at which sound is at full volume (from OGG metadata)
    void UpdatePosition(const Fvector& pos, float minDistance = 1.0f);

    // Get simulation results (call after simulation completes)
    float GetOcclusion() const;
    float GetSmoothedOcclusion(float dt);  // Smoothed occlusion for gradual transitions
    void GetTransmission(float out[3]) const;
    float GetDistanceAttenuation() const;

    // Get IPLSource for adding to simulator
    IPLSource GetSource() const { return m_source; }

    // Fetch outputs from simulator (call after iplSimulatorRunDirect)
    void FetchOutputs();

    // Process audio buffer with direct effects (occlusion, transmission, air absorption).
    // Input/Output: mono s16 PCM, modified in-place.
    void ProcessBuffer(s16* buffer, int numSamples, int sampleRate);

private:
    IPLSource m_source = nullptr;
    IPLSimulator m_simulator = nullptr;  // Cached for defensive iplSourceRemove in Destroy

    IPLDirectEffect m_directEffect = nullptr;

    // Simulation state
    IPLSimulationInputs m_inputs = {};
    IPLSimulationOutputs m_outputs = {};

    // Processing buffers (float, one frameSize each)
    xr_vector<float> m_inputBuffer;
    xr_vector<float> m_outputBuffer;

    // Cached values
    Fvector m_position = {0, 0, 0};
    bool m_outputsValid = false;
    float m_smoothedOcclusion = 1.0f;  // Start with no occlusion (1.0 = sound passes through)
};
