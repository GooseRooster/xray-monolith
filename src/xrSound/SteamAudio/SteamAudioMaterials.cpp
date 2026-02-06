#include "stdafx.h"
#include "SteamAudioMaterials.h"

extern int g_SA_DebugLogging;

namespace SteamAudioMaterials
{
    // Config state
    static bool s_configLoaded = false;
    static xr_map<u32, IPLint32> s_customMappings;

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
                // Format: material_id = preset_name
                u32 matId = atoi(item.first.c_str());
                MaterialPreset preset = GetPresetByName(item.second.c_str());
                s_customMappings[matId] = preset;
            }
            if (g_SA_DebugLogging)
                Msg("STEAM_AUDIO: Loaded %d material mappings", s_customMappings.size());
        }

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
        // Restore compile-time defaults so overrides from previous level don't persist
        for (int i = 0; i < MAT_COUNT; i++)
            s_presets[i] = s_defaultPresets[i];
    }

    IPLint32 MapMaterialId(u32 xrayMaterialId)
    {
        // Check custom mappings first
        if (s_configLoaded && !s_customMappings.empty())
        {
            auto it = s_customMappings.find(xrayMaterialId);
            if (it != s_customMappings.end())
                return it->second;
        }

        // Fallback to heuristic-based mapping
        // X-Ray material IDs are defined in gamemtl.ltx
        // Common material ID ranges (approximate, based on Anomaly):
        // 0-10: Terrain (dirt, grass, sand)
        // 11-30: Concrete, brick, asphalt
        // 31-50: Metal types
        // 51-70: Wood types
        // 71-90: Glass, plastic
        // 91-110: Fabric, carpet
        // 111+: Various other materials

        // Simple range-based mapping
        if (xrayMaterialId <= 5)
            return MAT_DIRT;
        else if (xrayMaterialId <= 10)
            return MAT_GRASS;
        else if (xrayMaterialId <= 30)
            return MAT_CONCRETE;
        else if (xrayMaterialId <= 50)
            return MAT_METAL;
        else if (xrayMaterialId <= 70)
            return MAT_WOOD;
        else if (xrayMaterialId <= 90)
            return MAT_GLASS;
        else if (xrayMaterialId <= 110)
            return MAT_FABRIC;
        else
            return MAT_DEFAULT;
    }
}
