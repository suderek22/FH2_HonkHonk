#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

struct HornAudio {
    std::vector<std::uint8_t> wave;
};

bool load_wave(const std::filesystem::path& path, HornAudio& audio,
               std::wstring& error);
bool play_horn(const HornAudio& audio);
bool play_horn_loop(const HornAudio& audio);
void stop_horn();
