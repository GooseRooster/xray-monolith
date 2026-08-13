#include "../stdafx.h"
#include "SteamAudioMaterials.h"

extern int g_SA_DebugLogging;

namespace SteamAudioMaterials
{
    // Config state
    static bool s_configLoaded = false;
    static xr_map<u32, IPLint32> s_customMappings;

    // Material names from gamemtl.xr, indexed by vector index (for diagnostics)
    static xr_vector<shared_str> s_materialNames;

    // Diagnostics: track which vector indices hit the fallback path
    static xr_map<u32, int> s_fallbackHitCounts;  // vectorIndex -> triangle count
    static int s_totalMappingCalls = 0;
    static int s_totalFallbackHits = 0;

    // Compile-time default presets — used to restore s_presets on Reset()
    // Format: absorption[3], scattering, transmission[3]
    // Values are 3-band: low (~250Hz), mid (~1kHz), high (~4kHz)
    // NOTE: Transmission values are intentionally higher than physically accurate
    // to maintain gameplay audibility while still providing filtering effect.
    static const IPLMaterial s_defaultPresets[MAT_COUNT] = {
        // MAT_DEFAULT - Generic moderate absorption
        {{0.10f, 0.20f, 0.30f}, 0.05f, {0.150f, 0.100f, 0.060f}},
        // MAT_CONCRETE
        {{0.01f, 0.02f, 0.02f}, 0.05f, {0.120f, 0.080f, 0.050f}},
        // MAT_METAL
        {{0.02f, 0.02f, 0.03f}, 0.10f, {0.300f, 0.250f, 0.180f}},
        // MAT_WOOD
        {{0.11f, 0.07f, 0.06f}, 0.05f, {0.180f, 0.140f, 0.100f}},
        // MAT_GLASS
        {{0.06f, 0.04f, 0.02f}, 0.05f, {0.400f, 0.350f, 0.280f}},
        // MAT_DIRT
        {{0.15f, 0.25f, 0.40f}, 0.20f, {0.200f, 0.150f, 0.100f}},
        // MAT_GRASS
        {{0.25f, 0.45f, 0.65f}, 0.30f, {0.180f, 0.120f, 0.080f}},
        // MAT_WATER
        {{0.99f, 0.99f, 0.99f}, 0.00f, {0.050f, 0.030f, 0.020f}},
        // MAT_FABRIC
        {{0.30f, 0.45f, 0.65f}, 0.15f, {0.140f, 0.100f, 0.060f}},
        // MAT_PLASTER
        {{0.08f, 0.06f, 0.05f}, 0.05f, {0.150f, 0.110f, 0.070f}},
    };

    // Mutable presets — LoadConfig() may override these from file
    static IPLMaterial s_presets[MAT_COUNT] = {
        // MAT_DEFAULT - Generic moderate absorption
        {{0.10f, 0.20f, 0.30f}, 0.05f, {0.150f, 0.100f, 0.060f}},

        // MAT_CONCRETE - Hard, reflective surface (increased transmission for gameplay)
        {{0.01f, 0.02f, 0.02f}, 0.05f, {0.120f, 0.080f, 0.050f}},

        // MAT_METAL - Very reflective, some high-freq absorption
        {{0.02f, 0.02f, 0.03f}, 0.10f, {0.300f, 0.250f, 0.180f}},

        // MAT_WOOD - Moderate absorption, good transmission
        {{0.11f, 0.07f, 0.06f}, 0.05f, {0.180f, 0.140f, 0.100f}},

        // MAT_GLASS - Low absorption, high transmission
        {{0.06f, 0.04f, 0.02f}, 0.05f, {0.400f, 0.350f, 0.280f}},

        // MAT_DIRT - High absorption, very diffuse (outdoor terrain, higher trans)
        {{0.15f, 0.25f, 0.40f}, 0.20f, {0.200f, 0.150f, 0.100f}},

        // MAT_GRASS - High absorption, very diffuse (outdoor terrain, higher trans)
        {{0.25f, 0.45f, 0.65f}, 0.30f, {0.180f, 0.120f, 0.080f}},

        // MAT_WATER - Near-total absorption (for gameplay, not physics)
        {{0.99f, 0.99f, 0.99f}, 0.00f, {0.050f, 0.030f, 0.020f}},

        // MAT_FABRIC - High absorption across all frequencies
        {{0.30f, 0.45f, 0.65f}, 0.15f, {0.140f, 0.100f, 0.060f}},

        // MAT_PLASTER - Similar to concrete but softer
        {{0.08f, 0.06f, 0.05f}, 0.05f, {0.150f, 0.110f, 0.070f}},
    };

    // Preset name lookup for config parsing
    static const char* s_presetNames[MAT_COUNT] = {
        "default", "concrete", "metal", "wood", "glass",
        "dirt", "grass", "water", "fabric", "plaster"
    };

    static MaterialPreset GetPresetByName(const char* name)
    {
        for (int i = 0; i < MAT_COUNT; i++)
        {
            if (_stricmp(name, s_presetNames[i]) == 0)
                return (MaterialPreset)i;
        }
        return MAT_DEFAULT;
    }

    // Parse gamemtl.xr to build gamemtl ID → vector index mapping, then
    // remap s_customMappings keys from gamemtl IDs to vector indices.
    // CDB::TRI.material stores vector indices (translated by Level_load.cpp),
    // but the config file maps by gamemtl IDs (stable human-readable identifiers).
    static void BuildMaterialTranslation()
    {
        string_path fn;
        if (!FS.exist(fn, "$game_data$", "gamemtl.xr"))
        {
            Msg("! STEAM_AUDIO: Cannot find gamemtl.xr — material mappings will use gamemtl IDs directly");
            return;
        }

        IReader* reader = FS.r_open(fn);
        if (!reader)
        {
            Msg("! STEAM_AUDIO: Cannot open gamemtl.xr");
            return;
        }

        IReader* mtlsChunk = reader->open_chunk(0x1002);  // GAMEMTLS_CHUNK_MTLS
        if (!mtlsChunk)
        {
            Msg("! STEAM_AUDIO: gamemtl.xr missing materials chunk");
            FS.r_close(reader);
            return;
        }

        // Iterate materials in file order (which matches GMLib.materials vector order).
        // Build gamemtl ID → vector index map, and store names for diagnostics.
        xr_map<u32, u32> mtlIdToVecIdx;
        s_materialNames.clear();

        u32 chunkId;
        for (IReader* O = mtlsChunk->open_chunk_iterator(chunkId);
             O;
             O = mtlsChunk->open_chunk_iterator(chunkId, O))
        {
            if (O->find_chunk(0x1000))  // GAMEMTL_CHUNK_MAIN
            {
                u32 matId = O->r_u32();
                shared_str name;
                O->r_stringZ(name);

                u32 vecIdx = (u32)s_materialNames.size();
                mtlIdToVecIdx[matId] = vecIdx;
                s_materialNames.push_back(name);
            }
        }
        mtlsChunk->close();
        FS.r_close(reader);

        if (s_customMappings.empty())
            return;

        // Remap s_customMappings: gamemtl ID keys → vector index keys
        xr_map<u32, IPLint32> remapped;
        for (const auto& kv : s_customMappings)
        {
            auto it = mtlIdToVecIdx.find(kv.first);
            if (it != mtlIdToVecIdx.end())
            {
                remapped[it->second] = kv.second;
                if (g_SA_DebugLogging)
                {
                    const char* name = (it->second < s_materialNames.size())
                        ? s_materialNames[it->second].c_str() : "?";
                    Msg("STEAM_AUDIO: Remap gamemtl ID %d (%s) -> vec idx %d -> preset %d",
                        kv.first, name, it->second, (int)kv.second);
                }
            }
            else if (g_SA_DebugLogging)
            {
                Msg("STEAM_AUDIO: Warning - gamemtl ID %d from config not found in gamemtl.xr", kv.first);
            }
        }

        s_customMappings = std::move(remapped);
        Msg("STEAM_AUDIO: Remapped %d material entries (gamemtl ID -> vector index)",
            (int)s_customMappings.size());
    }

    xr_vector<IPLMaterial> GetMaterialPresets()
    {
        xr_vector<IPLMaterial> result;
        result.reserve(MAT_COUNT);
        for (int i = 0; i < MAT_COUNT; i++)
        {
            result.push_back(s_presets[i]);
        }
        return result;
    }

    IPLMaterial GetMaterialPreset(MaterialPreset preset)
    {
        if (preset >= 0 && preset < MAT_COUNT)
            return s_presets[preset];
        return s_presets[MAT_DEFAULT];
    }

    void LoadConfig()
    {
        if (s_configLoaded)
            return;

        string_path fn;
        if (!FS.exist(fn, "$game_config$", "sound\\steam_audio_materials.ltx"))
        {
            if (g_SA_DebugLogging)
                Msg("STEAM_AUDIO: No material config found, using defaults");
            s_configLoaded = true;
            return;
        }

        if (g_SA_DebugLogging)
            Msg("STEAM_AUDIO: Loading material config from %s", fn);

        CInifile config(fn);

        // Load custom presets if [presets] section exists
        if (config.section_exist("presets"))
        {
            for (int i = 0; i < MAT_COUNT; i++)
            {
                const char* presetName = s_presetNames[i];
                if (config.line_exist("presets", presetName))
                {
                    // Format: "abs_low, abs_mid, abs_high, scatter, trans_low, trans_mid, trans_high"
                    LPCSTR values = config.r_string("presets", presetName);

                    float abs_low, abs_mid, abs_high, scatter, trans_low, trans_mid, trans_high;
                    if (sscanf(values, "%f, %f, %f, %f, %f, %f, %f",
                        &abs_low, &abs_mid, &abs_high, &scatter,
                        &trans_low, &trans_mid, &trans_high) == 7)
                    {
                        s_presets[i].absorption[0] = abs_low;
                        s_presets[i].absorption[1] = abs_mid;
                        s_presets[i].absorption[2] = abs_high;
                        s_presets[i].scattering = scatter;
                        s_presets[i].transmission[0] = trans_low;
                        s_presets[i].transmission[1] = trans_mid;
                        s_presets[i].transmission[2] = trans_high;

                        if (g_SA_DebugLogging)
                            Msg("STEAM_AUDIO: Loaded preset '%s'", presetName);
                    }
                }
            }
        }

        // Load material ID mappings if [mappings] section exists
        if (config.section_exist("mappings"))
        {
            CInifile::Sect& sect = config.r_section("mappings");
            for (auto& item : sect.Data)
            {
                const char* rawKey = item.first.c_str();
                const char* rawVal = item.second.c_str();
                if (!rawKey || !rawVal)
                    continue;

                u32 matId = atoi(rawKey);
                MaterialPreset preset = GetPresetByName(rawVal);
                s_customMappings[matId] = preset;

                if (g_SA_DebugLogging)
                    Msg("STEAM_AUDIO: Mapped ID %d -> '%s' (preset %d)", matId, rawVal, (int)preset);
            }
            Msg("STEAM_AUDIO: Loaded %d material mappings (by gamemtl ID)", (int)s_customMappings.size());
        }

        // Remap s_customMappings from gamemtl IDs to vector indices.
        // CDB triangles store vector indices (translated by Level_load.cpp),
        // so MapMaterialId() needs vector-index keys to match.
        BuildMaterialTranslation();

        s_configLoaded = true;
    }

    bool IsConfigLoaded()
    {
        return s_configLoaded;
    }

    void Reset()
    {
        s_configLoaded = false;
        s_customMappings.clear();
        s_materialNames.clear();
        // Restore compile-time defaults so overrides from previous level don't persist
        for (int i = 0; i < MAT_COUNT; i++)
            s_presets[i] = s_defaultPresets[i];
    }

    IPLint32 MapMaterialId(u32 vectorIndex)
    {
        s_totalMappingCalls++;

        // Lookup by vector index (remapped from gamemtl IDs during LoadConfig)
        if (s_configLoaded && !s_customMappings.empty())
        {
            auto it = s_customMappings.find(vectorIndex);
            if (it != s_customMappings.end())
                return it->second;
        }

        // Fallback: MAT_DEFAULT for unmapped indices.
        // Use LogUnmappedSummary() to find indices that need explicit
        // mappings in steam_audio_materials.ltx.
        s_totalFallbackHits++;
        s_fallbackHitCounts[vectorIndex]++;
        return MAT_DEFAULT;
    }

    void ResetDiagnostics()
    {
        s_fallbackHitCounts.clear();
        s_totalMappingCalls = 0;
        s_totalFallbackHits = 0;
    }

    void LogUnmappedSummary()
    {
        if (s_totalMappingCalls == 0)
            return;

        int mappedCount = s_totalMappingCalls - s_totalFallbackHits;
        float mappedPct = 100.0f * mappedCount / s_totalMappingCalls;

        Msg("STEAM_AUDIO: Material mapping summary: %d/%d triangles mapped (%.1f%%), %d unmapped",
            mappedCount, s_totalMappingCalls, mappedPct, s_totalFallbackHits);

        if (s_fallbackHitCounts.empty())
            return;

        // Sort unmapped IDs by triangle count (descending) and log top 20
        xr_vector<std::pair<u32, int>> sorted;
        sorted.reserve(s_fallbackHitCounts.size());
        for (const auto& kv : s_fallbackHitCounts)
            sorted.push_back(std::make_pair(kv.first, kv.second));
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        int logCount = std::min((int)sorted.size(), 20);
        Msg("STEAM_AUDIO: Top %d unmapped materials (add to [mappings] in steam_audio_materials.ltx):", logCount);
        for (int i = 0; i < logCount; i++)
        {
            const char* name = (sorted[i].first < s_materialNames.size())
                ? s_materialNames[sorted[i].first].c_str() : "unknown";
            Msg("  vecIdx %3d (%s) = %d triangles", sorted[i].first, name, sorted[i].second);
        }
    }
}
