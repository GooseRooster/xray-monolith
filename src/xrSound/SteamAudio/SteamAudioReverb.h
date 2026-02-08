#pragma once

#include <phonon.h>
#include <algorithm>
#include <AL/al.h>

class CSteamAudioScene;
class CSoundRender_Environment;

extern int psSA_Convolution;
extern float psSA_ConvolutionGain;

/**
 * CSteamAudioReverb - Listener reverb probe for geometry-aware reverb.
 *
 * Two modes of operation:
 *
 * PARAMETRIC (psSA_Convolution=0):
 *   Maintains a single persistent IPLSource at the listener position that runs
 *   reflections simulation in PARAMETRIC mode. The simulator traces rays and
 *   analyzes the sound field to produce reverbTimes[3] (low/mid/high RT60).
 *   All 26 EAX reverb parameters are derived from these three values using
 *   acoustic heuristics, then smoothed and fed to EFX.
 *
 * CONVOLUTION (psSA_Convolution=1):
 *   The simulator produces an actual impulse response (IR) instead of parametric
 *   RT60 values. The accumulated source mix is convolved with this IR via
 *   iplReflectionEffectApply, decoded from ambisonics to stereo, and streamed
 *   to a dedicated OpenAL source. EFX is disabled when this is active.
 *   reverbTimes are still available for gain control heuristics.
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

    // Whether the probe has received at least one valid result
    bool HasValidData() const { return m_hasValidData; }

    // Fill environment with SA-derived reverb parameters (all 26 EAX params).
    // The environment's version is set to sndenv_ver_extended.
    void GetEnvironment(CSoundRender_Environment& env) const;

    // Reverb enable state
    void SetReverbEnabled(bool enabled) { m_enabled = enabled; }
    bool IsReverbEnabled() const { return m_enabled; }

    // Convolution reverb
    bool IsConvolutionActive() const { return m_convolutionInitialized; }
    void UpdateConvolution();

private:
    // Compute raw EFX parameters from reverbTimes[3]
    void DeriveParameters();

    // Convolution reverb init/destroy
    bool InitializeConvolution(CSteamAudioScene* scene);
    void DestroyConvolution();

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

    // --- Median-of-3 filter for RT60 spike rejection ---
    float m_rt60History[3][3] = {};  // [band][sample] ring buffer
    float m_filteredRT60[3] = {};
    int m_historyIndex = 0;

    // --- State ---
    bool m_enabled = true;
    IPLSimulator m_simulator = nullptr;

    // --- Convolution reverb state ---
    bool m_convolutionInitialized = false;

    // IPL processing objects
    IPLReflectionEffect m_reflectionEffect = nullptr;
    IPLAmbisonicsDecodeEffect m_ambisonicsDecoder = nullptr;
    IPLHRTF m_hrtf = nullptr;  // Required by ambisonics decode API but we use PANNING mode

    // Processing buffers
    static constexpr int AMBI_CHANNELS = 4;   // 1st-order ambisonics
    static constexpr int STEREO_CHANNELS = 2;
    static constexpr int NUM_AL_BUFFERS = 8;  // Ring buffer depth (~186ms)

    int m_frameSize = 0;
    int m_samplingRate = 0;

    xr_vector<float> m_monoInputData;                      // frameSize
    xr_vector<float> m_ambiChannelData[AMBI_CHANNELS];     // frameSize each
    xr_vector<float> m_stereoChannelData[STEREO_CHANNELS]; // frameSize each
    xr_vector<s16>   m_stereoS16;                          // frameSize * 2 (interleaved)

    // OpenAL streaming
    ALuint m_reverbSource = 0;
    ALuint m_reverbBuffers[NUM_AL_BUFFERS] = {};

    // Gain control
    float m_smoothedConvGain = 0.5f;

    // Temp buffer for draining per-source ring buffers (reused each UpdateConvolution call)
    xr_vector<float> m_tempDrainFrame;
};
