#pragma once
#include "EQEngine.h"
#include <vector>

namespace eqm
{
struct PresetBand
{
    FilterType type; double freq, gain, q;
    ChannelMode mode = ChannelMode::Stereo;
    bool dyn = false; double thr = -30.0, ratio = 3.0, range = 6.0;
};

// target: 0 = -24, 1 = -23, 2 = -16, 3 = -14, 4 = -12, 5 = -10, 6 = custom
struct Preset { const char* name; std::vector<PresetBand> bands; int target; };

inline const std::vector<Preset>& factoryPresets()
{
    using T = FilterType;
    static const std::vector<Preset> presets = {
        { "Default", {}, 3 },
        { "Vocal", {
            { T::HighPass, 90, 0, 0.707 }, { T::Bell, 250, -2.5, 1.2 }, { T::Bell, 3000, 2.0, 1.0 },
            { T::HighShelf, 10000, 2.5, 0.7 },
            { T::Bell, 6500, 0, 4.0, ChannelMode::Stereo, true, -35, 4, 6 } }, 3 },
        { "Voice", {
            { T::HighPass, 100, 0, 0.707 }, { T::Bell, 200, -2.0, 1.0 }, { T::Bell, 2500, 2.5, 1.0 },
            { T::HighShelf, 9000, 1.5, 0.7 },
            { T::Bell, 6000, 0, 4.0, ChannelMode::Stereo, true, -35, 4, 5 } }, 2 },
        { "Podcast", {
            { T::HighPass, 80, 0, 0.707 }, { T::Bell, 120, 1.5, 0.8 }, { T::Bell, 300, -2.0, 1.0 },
            { T::Bell, 3500, 2.0, 0.9 }, { T::HighShelf, 8000, 1.5, 0.7 } }, 2 },
        { "Bass", {
            { T::HighPass, 30, 0, 0.707 }, { T::LowShelf, 80, 2.0, 0.7 }, { T::Bell, 250, -3.0, 1.2 },
            { T::Bell, 800, 1.5, 1.0 }, { T::Bell, 2500, 2.0, 1.0 } }, -1 },
        { "Kick", {
            { T::HighPass, 30, 0, 0.707 }, { T::Bell, 60, 3.0, 1.2 }, { T::Bell, 350, -4.0, 1.5 },
            { T::Bell, 3500, 3.0, 1.0 } }, -1 },
        { "Snare", {
            { T::HighPass, 80, 0, 0.707 }, { T::Bell, 200, 2.5, 1.2 }, { T::Bell, 500, -2.5, 1.5 },
            { T::Bell, 5000, 3.0, 1.0 }, { T::HighShelf, 10000, 2.0, 0.7 } }, -1 },
        { "Drums", {
            { T::HighPass, 35, 0, 0.707 }, { T::Bell, 80, 2.0, 1.0 }, { T::Bell, 400, -2.5, 1.0 },
            { T::Bell, 4000, 2.0, 1.0 }, { T::HighShelf, 12000, 2.0, 0.7 } }, -1 },
        { "Guitar", {
            { T::HighPass, 80, 0, 0.707 }, { T::Bell, 250, -2.0, 1.0 }, { T::Bell, 3000, 1.5, 1.0 },
            { T::HighShelf, 9000, 1.5, 0.7 } }, -1 },
        { "Acoustic Guitar", {
            { T::HighPass, 80, 0, 0.707 }, { T::Bell, 200, -2.0, 1.0 }, { T::Bell, 3000, 1.5, 1.0 },
            { T::HighShelf, 10000, 3.0, 0.7 } }, -1 },
        { "Electric Guitar", {
            { T::HighPass, 90, 0, 0.707 }, { T::Bell, 300, -2.5, 1.0 }, { T::Bell, 800, 1.0, 1.0 },
            { T::Bell, 2500, 2.5, 1.0 }, { T::LowPass, 12000, 0, 0.707 } }, -1 },
        { "Piano", {
            { T::HighPass, 40, 0, 0.707 }, { T::Bell, 300, -2.0, 1.0 }, { T::Bell, 3000, 1.5, 1.0 },
            { T::HighShelf, 10000, 2.0, 0.7 } }, -1 },
        { "Keys", {
            { T::HighPass, 60, 0, 0.707 }, { T::Bell, 400, -2.0, 1.0 }, { T::Bell, 2000, 1.5, 1.0 },
            { T::HighShelf, 8000, 1.5, 0.7 } }, -1 },
        { "Master", {
            { T::HighPass, 25, 0, 0.707 }, { T::LowShelf, 80, 1.0, 0.7 }, { T::Bell, 300, -1.0, 0.8 },
            { T::Bell, 3000, 0.8, 0.7 }, { T::HighShelf, 12000, 1.5, 0.7 } }, 3 },
        { "Streaming", {
            { T::HighPass, 25, 0, 0.707 }, { T::Bell, 200, -1.0, 0.9 }, { T::HighShelf, 11000, 1.2, 0.7 } }, 3 },
        { "YouTube", {
            { T::HighPass, 30, 0, 0.707 }, { T::Bell, 250, -1.5, 0.9 }, { T::Bell, 3000, 1.5, 0.9 },
            { T::HighShelf, 10000, 2.0, 0.7 } }, 3 },
        { "Radio", {
            { T::HighPass, 60, 0, 0.707 }, { T::LowShelf, 100, 2.0, 0.7 }, { T::Bell, 3000, 2.0, 0.8 },
            { T::LowPass, 15000, 0, 0.707 } }, 2 },
        { "Broadcast", {
            { T::HighPass, 50, 0, 0.707 }, { T::Bell, 250, -2.0, 1.0 }, { T::Bell, 3000, 1.5, 0.9 },
            { T::LowPass, 15000, 0, 0.707 } }, 1 },
        { "Live", {
            { T::HighPass, 100, 0, 0.707 }, { T::Bell, 250, -3.0, 1.5 }, { T::Bell, 3000, 1.5, 1.0 },
            { T::HighShelf, 8000, 1.0, 0.7 } }, -1 },
    };
    return presets;
}
} // namespace eqm
