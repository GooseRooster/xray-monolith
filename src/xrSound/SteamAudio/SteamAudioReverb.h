#pragma once

#include <phonon.h>

class CSteamAudioScene;
class CSoundRender_Environment;

/**
 * CSteamAudioReverb - Listener reverb probe for geometry-aware EFX parameters.
 *
 * Maintains a single persistent IPLSource at the listener position that runs
 * reflections simulation in PARAMETRIC mode. The simulator traces rays and
 * analyzes the sound field to produce reverbTimes[3] (low/mid/high RT60).
 *
 * All 26 EAX reverb parameters are derived from these three values using
 * acoustic heuristics, then smoothed internally to prevent jarring transitions.
 * The result is a complete CSoundRender_Environment that replaces the baked
 * sound environments entirely when SA reverb is active.
 */
class CSteamAudioReverb
{
public:
    CSteamAudioReverb();
    ~CSteamAudioReverb();

    // Initialize with scene (needs simulator for reflections)
    bool Initialize(CSteamAudioScene* scene);
    void Destroy();

    bool IsInitialized() const { return m_listenerSource != nullptr; }

    // Update listener probe position (called from update_listener each frame)
    void UpdateListenerProbe(const Fvector& pos, const Fvector& dir, const Fvector& up);

    // Fetch latest simulation outputs and recompute derived EFX parameters.
    // Call once per frame (or per reverb update interval).
    // dt: time delta in seconds for smoothing
    void UpdateProbe(float dt);

    // Whether the probe has received at least one valid reverbTimes result
    bool HasValidData() const { return m_hasValidData; }

    // Fill environment with SA-derived reverb parameters (all 26 EAX params).
    // The environment's version is set to sndenv_ver_extended.
    void GetEnvironment(CSoundRender_Environment& env) const;

    // Reverb enable state
    void SetReverbEnabled(bool enabled) { m_enabled = enabled; }
    bool IsReverbEnabled() const { return m_enabled; }

private:
    // Compute raw EFX parameters from reverbTimes[3]
    void DeriveParameters();

    // --- Listener reverb probe ---
    IPLSource m_listenerSource = nullptr;
    IPLSimulationInputs m_listenerInputs = {};
    IPLSimulationOutputs m_listenerOutputs = {};
    bool m_hasValidData = false;

    // --- Smoothed EFX parameters (computed in DeriveParameters, smoothed in UpdateProbe) ---
    struct EFXParams
    {
        float DecayTime = 1.49f;
        float DecayHFRatio = 0.83f;
        float DecayLFRatio = 1.0f;
        float Room = 0.32f;
        float RoomHF = 0.89f;
        float RoomLF = 1.0f;
        float Density = 1.0f;
        float Diffusion = 1.0f;
        float Reflections = 0.05f;
        float ReflectionsDelay = 0.007f;
        float Reverb = 1.26f;
        float ReverbDelay = 0.011f;
        float EchoTime = 0.25f;
        float EchoDepth = 0.0f;
        float AirAbsorptionHF = 0.994f;
        float RoomRolloffFactor = 0.0f;
        int   DecayHFLimit = 1;
        float ModulationTime = 0.25f;
        float ModulationDepth = 0.0f;
        float HFReference = 5000.0f;
        float LFReference = 250.0f;
    };

    EFXParams m_rawParams;       // Freshly computed from reverbTimes (no smoothing)
    EFXParams m_smoothedParams;  // Exponentially smoothed for output

    // --- State ---
    bool m_enabled = true;
    IPLSimulator m_simulator = nullptr;
};
