#pragma once

#include <phonon.h>
#include <algorithm>
#include <AL/al.h>

class CSteamAudioScene;

extern float psSA_ConvolutionGain;
extern float psSA_ConvolutionLPF;
extern float psSA_ReverbScaleLow;
extern float psSA_ReverbScaleMid;
extern float psSA_ReverbScaleHigh;

/**
 * CSteamAudioReverb - Listener reverb probe for geometry-aware reverb.
 *
 * Always operates in HYBRID mode when Steam Audio is enabled:
 * SA traces rays for the full 2.0s duration, producing a complete IR.
 * iplReflectionEffectApply convolves the early portion (controlled by
 * hybridReverbTransitionTime, quality-dependent: 0.3s medium, 0.5s high),
 * crossfades, then an internal parametric FDN handles the late tail
 * shaped by reverbTimes[3] and eq[3]. The EFX slot is set to AL_EFFECT_NULL.
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

    // Fetch latest simulation outputs.
    // Call once per frame (or per reverb update interval).
    // dt: time delta in seconds
    void UpdateProbe(float dt);

    // Whether the probe has received at least one valid result
    bool HasValidData() const { return m_hasValidData; }

    // Reverb enable state
    void SetReverbEnabled(bool enabled) { m_enabled = enabled; }
    bool IsReverbEnabled() const { return m_enabled; }

    // Convolution reverb
    bool IsConvolutionActive() const { return m_convolutionInitialized; }
    void UpdateConvolution();

    // Recreate OpenAL resources after device switch (convolution reverb only)
    void ReinitializeOpenAL();

private:
    // Convolution reverb init/destroy
    bool InitializeConvolution(CSteamAudioScene* scene);
    void DestroyConvolution();

    // --- Listener reverb probe ---
    IPLSource m_listenerSource = nullptr;
    IPLSimulationInputs m_listenerInputs = {};
    IPLSimulationOutputs m_listenerOutputs = {};
    bool m_hasValidData = false;

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
    static constexpr int AMBI_CHANNELS = 9;   // 2nd-order ambisonics
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

    // Low-pass filter state for reverb tail darkening
    float m_lpStateL = 0.0f;
    float m_lpStateR = 0.0f;

    // Temp buffer for draining per-source ring buffers (reused each UpdateConvolution call)
    xr_vector<float> m_tempDrainFrame;
};
