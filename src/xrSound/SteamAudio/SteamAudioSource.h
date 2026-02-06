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
 * - IPLBinauralEffect for HRTF spatialization of direct sound
 * - Simulation inputs/outputs
 * - Audio buffer processing
 *
 * Reverb is handled globally by CSteamAudioReverb's listener probe.
 * Sources contribute their dry audio to the shared reverb bus via AccumulateDryAudio().
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

    // Process audio buffer with direct effects (occlusion, transmission, air absorption)
    // and optionally apply binaural HRTF. Contributes dry audio to reverb bus.
    // Input: mono s16 PCM
    // Output: modified in place (mono if !binaural, interleaved stereo if binaural)
    // Returns: true if output is stereo (binaural enabled), false if mono
    bool ProcessBuffer(s16* buffer, int numSamples, int sampleRate,
                       const Fvector& listenerPos, const Fvector& listenerDir, const Fvector& listenerUp);

private:
    IPLSource m_source = nullptr;
    IPLSimulator m_simulator = nullptr;  // Cached for defensive iplSourceRemove in Destroy
    IPLDirectEffect m_directEffect = nullptr;
    IPLBinauralEffect m_binauralEffect = nullptr;      // For HRTF spatialization of direct sound

    // Simulation state
    IPLSimulationInputs m_inputs = {};
    IPLSimulationOutputs m_outputs = {};

    // Processing buffers (float, deinterleaved)
    xr_vector<float> m_inputBuffer;
    xr_vector<float> m_outputBuffer;

    // Stereo output buffer for binaural processing
    xr_vector<float> m_stereoData;       // 2 * frameSize floats
    xr_vector<float*> m_stereoChannels;  // [0] = left, [1] = right
    IPLAudioBuffer m_stereoBuffer = {};

    // Cached values
    Fvector m_position = {0, 0, 0};
    bool m_outputsValid = false;
    float m_smoothedOcclusion = 1.0f;  // Start with no occlusion (1.0 = sound passes through)
    bool m_hasBinauralEffect = false;    // True if binaural effect was created
};
