#include "stdafx.h"
#include "SteamAudio.h"
#include <ctime>  // For clock() in rate-limited logging

extern int g_SA_DebugLogging;

// Static member initialization for rate-limited error logging
int CSteamAudio::s_validationErrorCount = 0;
float CSteamAudio::s_lastErrorLogTime = 0.0f;

CSteamAudio& CSteamAudio::Instance()
{
    static CSteamAudio instance;
    return instance;
}

CSteamAudio::CSteamAudio()
{
}

CSteamAudio::~CSteamAudio()
{
    Shutdown();
}

void IPLCALL CSteamAudio::LogCallback(IPLLogLevel level, const char* message)
{
    switch (level)
    {
    case IPL_LOGLEVEL_INFO:
        if (g_SA_DebugLogging)
            Msg("STEAM_AUDIO: %s", message);
        break;

    case IPL_LOGLEVEL_WARNING:
    case IPL_LOGLEVEL_ERROR:
        {
            // Rate-limit error/warning logging to prevent log spam and memory issues
            s_validationErrorCount++;

            // Get current time - use simple timer if available
            float currentTime = (float)clock() / CLOCKS_PER_SEC;

            if (currentTime - s_lastErrorLogTime > ERROR_LOG_INTERVAL)
            {
                if (level == IPL_LOGLEVEL_ERROR)
                    Msg("! STEAM_AUDIO ERROR: %s (count: %d)", message, s_validationErrorCount);
                else
                    Msg("! STEAM_AUDIO WARNING: %s (count: %d)", message, s_validationErrorCount);

                s_lastErrorLogTime = currentTime;

                // Warn user if too many errors - may indicate configuration issue
                if (s_validationErrorCount == MAX_ERRORS_BEFORE_WARNING)
                {
                    Msg("! STEAM_AUDIO: High error count detected. Check Steam Audio configuration.");
                }
            }
        }
        break;

    case IPL_LOGLEVEL_DEBUG:
#ifdef DEBUG
        Msg("STEAM_AUDIO DEBUG: %s", message);
#endif
        break;
    }
}

bool CSteamAudio::Initialize()
{
    if (m_bInitialized)
        return true;

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Initializing Steam Audio...");

    // Context settings
    IPLContextSettings contextSettings = {};
    contextSettings.version = STEAMAUDIO_VERSION;
    contextSettings.logCallback = &CSteamAudio::LogCallback;
    contextSettings.allocateCallback = nullptr;  // Use default allocator
    contextSettings.freeCallback = nullptr;
    // SIMD level selection based on build configuration
    // AVX build can safely use AVX2 (all AVX-capable CPUs support AVX2)
    // Non-AVX build leaves at 0 (zero-init) for auto-detection
#ifdef __AVX__
    contextSettings.simdLevel = IPL_SIMDLEVEL_AVX2;
#endif

    // Enable validation only in debug builds - has significant performance penalty
#ifdef DEBUG
    contextSettings.flags = IPL_CONTEXTFLAGS_VALIDATION;
#endif

    IPLerror error = iplContextCreate(&contextSettings, &m_context);
    if (error != IPL_STATUS_SUCCESS)
    {
        Msg("! STEAM_AUDIO: Failed to create context (error: %d)", error);
        return false;
    }

    // Audio settings - match X-Ray engine audio format
    m_audioSettings.samplingRate = 44100;
    m_audioSettings.frameSize = 1024;  // ~23ms at 44.1kHz

    m_bInitialized = true;
    s_validationErrorCount = 0;
    s_lastErrorLogTime = 0.0f;
    if (g_SA_DebugLogging)
    {
#ifdef __AVX__
        Msg("STEAM_AUDIO: Initialized (SIMD: AVX2, Sample Rate: %d Hz, Frame Size: %d)",
#else
        Msg("STEAM_AUDIO: Initialized (SIMD: auto, Sample Rate: %d Hz, Frame Size: %d)",
#endif
            m_audioSettings.samplingRate, m_audioSettings.frameSize);
    }

    return true;
}

void CSteamAudio::Shutdown()
{
    if (!m_bInitialized)
        return;

    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Shutting down...");

    if (m_context)
    {
        iplContextRelease(&m_context);
        m_context = nullptr;
    }

    m_bInitialized = false;
    if (g_SA_DebugLogging)
        Msg("STEAM_AUDIO: Shutdown complete");
}
