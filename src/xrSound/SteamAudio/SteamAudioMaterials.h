#pragma once

#include <phonon.h>

/**
 * SteamAudioMaterials - Maps X-Ray material indices to acoustic properties.
 *
 * CDB collision triangles store a 14-bit material vector index (translated
 * from gamemtl IDs by Level_load.cpp). The config file uses gamemtl IDs
 * for readability; LoadConfig() parses gamemtl.xr to remap keys to vector
 * indices so MapMaterialId() lookups match what CDB stores.
 *
 * Acoustic properties are 3-band (low, mid, high frequency):
 * - absorption: How much sound is absorbed (0=reflect all, 1=absorb all)
 * - scattering: How diffusely sound reflects (0=specular, 1=diffuse)
 * - transmission: How much sound passes through (0=block all, 1=pass all)
 */
namespace SteamAudioMaterials
{
    // Material preset indices (used internally)
    enum MaterialPreset
    {
        MAT_DEFAULT = 0,    // Generic/unknown
        MAT_CONCRETE,       // Concrete, brick, stone
        MAT_METAL,          // Metal surfaces
        MAT_WOOD,           // Wood surfaces
        MAT_GLASS,          // Glass, windows
        MAT_DIRT,           // Dirt, gravel, earth
        MAT_GRASS,          // Grass, vegetation
        MAT_WATER,          // Water surfaces
        MAT_FABRIC,         // Cloth, carpet, curtains
        MAT_PLASTER,        // Plaster, drywall

        MAT_COUNT
    };

    // Load material configuration from file
    // Path: configs/sound/steam_audio_materials.ltx
    void LoadConfig();

    // Reset config state — call before level change so LoadConfig() re-reads
    void Reset();

    // Check if config has been loaded
    bool IsConfigLoaded();

    // Get the array of material presets
    xr_vector<IPLMaterial> GetMaterialPresets();

    // Map material vector index to preset index.
    // Takes the vector index stored in CDB::TRI.material (translated from
    // gamemtl ID by Level_load.cpp). Returns index into the presets array.
    IPLint32 MapMaterialId(u32 vectorIndex);

    // Get a specific material preset by index
    IPLMaterial GetMaterialPreset(MaterialPreset preset);

    // Diagnostics: track unmapped material IDs hitting the heuristic fallback
    void ResetDiagnostics();
    void LogUnmappedSummary();
}
