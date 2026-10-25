#include "HonkHonkAudio.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>

namespace {

constexpr std::size_t kWaveHeaderSize = 12;
constexpr std::size_t kPcmFormatSize = 16;
constexpr DWORD kMaximumWaveBytes = 32 * 1024 * 1024;
constexpr WORD kExpectedChannels = 2;
constexpr DWORD kExpectedSampleRate = 44100;
constexpr WORD kExpectedBitsPerSample = 16;
std::mutex g_playback_mutex;

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes,
                       std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                       std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

bool has_tag(const std::vector<std::uint8_t>& bytes, std::size_t offset,
             const char (&tag)[5]) {
    return offset + 4 <= bytes.size() &&
           std::equal(tag, tag + 4, bytes.begin() + offset);
}

} // namespace

bool load_wave(const std::filesystem::path& path, HornAudio& audio,
               std::wstring& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = L"Could not open the WAV file.";
        return false;
    }
    const std::streamoff file_size = file.tellg();
    if (file_size < static_cast<std::streamoff>(kWaveHeaderSize) ||
        file_size > kMaximumWaveBytes) {
        error = L"WAV file size is invalid or exceeds 32 MiB.";
        return false;
    }

    audio.wave.resize(static_cast<std::size_t>(file_size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(audio.wave.data()), file_size)) {
        error = L"Could not read the complete WAV file.";
        return false;
    }
    if (!has_tag(audio.wave, 0, "RIFF") ||
        !has_tag(audio.wave, 8, "WAVE") ||
        read_u32(audio.wave, 4) != audio.wave.size() - 8) {
        error = L"File does not contain a valid RIFF/WAVE header.";
        return false;
    }

    bool found_pcm_format = false;
    bool found_data = false;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;
    std::size_t offset = kWaveHeaderSize;
    while (offset + 8 <= audio.wave.size()) {
        const std::uint32_t chunk_size = read_u32(audio.wave, offset + 4);
        const std::size_t chunk_offset = offset + 8;
        if (chunk_size > audio.wave.size() - chunk_offset) {
            error = L"WAV chunk extends beyond the end of the file.";
            return false;
        }

        if (has_tag(audio.wave, offset, "fmt ")) {
            if (chunk_size < kPcmFormatSize ||
                read_u16(audio.wave, chunk_offset) != 1 ||
                read_u16(audio.wave, chunk_offset + 2) != kExpectedChannels ||
                read_u32(audio.wave, chunk_offset + 4) != kExpectedSampleRate ||
                read_u32(audio.wave, chunk_offset + 8) !=
                    kExpectedSampleRate * kExpectedChannels *
                        kExpectedBitsPerSample / 8 ||
                read_u16(audio.wave, chunk_offset + 12) !=
                    kExpectedChannels * kExpectedBitsPerSample / 8 ||
                read_u16(audio.wave, chunk_offset + 14) !=
                    kExpectedBitsPerSample) {
                error = L"WAV must be 44.1 kHz, 16-bit stereo PCM.";
                return false;
            }
            found_pcm_format = true;
        } else if (has_tag(audio.wave, offset, "data")) {
            data_offset = chunk_offset;
            data_size = chunk_size;
            found_data = true;
        }

        offset = chunk_offset + chunk_size + (chunk_size & 1);
    }

    if (!found_pcm_format || !found_data || data_size == 0 ||
        data_size % (kExpectedChannels * kExpectedBitsPerSample / 8) != 0) {
        error = L"WAV is missing a valid PCM format or audio data chunk.";
        return false;
    }

    return true;
}

bool play_horn(const HornAudio& audio) {
    if (audio.wave.size() < kWaveHeaderSize ||
        audio.wave.size() > std::numeric_limits<DWORD>::max()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_playback_mutex);
    return PlaySoundW(
        reinterpret_cast<LPCWSTR>(audio.wave.data()), nullptr,
        SND_ASYNC | SND_MEMORY | SND_NODEFAULT) != FALSE;
}

bool play_horn_loop(const HornAudio& audio) {
    if (audio.wave.size() < kWaveHeaderSize ||
        audio.wave.size() > std::numeric_limits<DWORD>::max()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_playback_mutex);
    return PlaySoundW(
        reinterpret_cast<LPCWSTR>(audio.wave.data()), nullptr,
        SND_ASYNC | SND_LOOP | SND_MEMORY | SND_NODEFAULT) != FALSE;
}

void stop_horn() {
    std::lock_guard<std::mutex> lock(g_playback_mutex);
    PlaySoundW(nullptr, nullptr, 0);
}
