#include "FmodHorn.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace fmodhorn {
namespace {

// Function offsets inside forza_durango_release_final.exe (see re\fh2dis.py).
constexpr std::uintptr_t kStudioUpdate = 0x1A9C7B0;
constexpr std::uintptr_t kStudioGetLowLevel = 0x1A98080;
constexpr std::uintptr_t kCreateSound = 0x1997740;
constexpr std::uintptr_t kPlaySound = 0x199B0F0;
constexpr std::uintptr_t kGetListener = 0x1997C00;
constexpr std::uintptr_t kSet3DAttributes = 0x19A3F50;
constexpr std::uintptr_t kSet3DMinMax = 0x19A4BC0;
constexpr std::uintptr_t kSetVolume = 0x19A6470;
constexpr std::uintptr_t kSetPaused = 0x19A5E50;
constexpr std::uintptr_t kStop = 0x19A6730;
constexpr std::uintptr_t kIsPlaying = 0x19A3B70;
constexpr std::uintptr_t kGetDsp = 0x19A2860;
constexpr std::uintptr_t kAddInput = 0x19A7A70;
constexpr std::uintptr_t kSetLoopPoints = 0x19A0960;
constexpr std::uintptr_t kMasterGroup = 0x1998C90;
constexpr std::uintptr_t kGroupName = 0x19A6DE0;
constexpr std::uintptr_t kGroupCount = 0x19A7050;
constexpr std::uintptr_t kGroupChild = 0x19A6C90;

constexpr unsigned kModeLoop = 0x2;
constexpr unsigned kMode3D = 0x10;
constexpr unsigned kModeCreateSample = 0x100;
constexpr unsigned kModeOpenMemory = 0x800;
constexpr unsigned kModeInverseRolloff = 0x100000;
constexpr unsigned kModeWorldRelative = 0x80000;
constexpr unsigned kExInfoSize = 0xD8;

struct Vec {
    float x, y, z;
};

using StudioUpdateFn = int(__fastcall*)(void*);
using GetLowLevelFn = int(__fastcall*)(void*, void**);
using CreateSoundFn = int(__fastcall*)(void*, const void*, unsigned, void*, void**);
using PlaySoundFn = int(__fastcall*)(void*, void*, void*, bool, void**);
using GetListenerFn = int(__fastcall*)(void*, int, Vec*, Vec*, Vec*, Vec*);
using Set3DFn = int(__fastcall*)(void*, const Vec*, const Vec*, const Vec*);
using MinMaxFn = int(__fastcall*)(void*, float, float);
using SetVolumeFn = int(__fastcall*)(void*, float);
using SetPausedFn = int(__fastcall*)(void*, bool);
using StopFn = int(__fastcall*)(void*);
using LoopPointsFn = int(__fastcall*)(void*, unsigned, unsigned, unsigned, unsigned);
using MasterGroupFn = int(__fastcall*)(void*, void**);
using GroupNameFn = int(__fastcall*)(void*, char*, int);
using GroupCountFn = int(__fastcall*)(void*, int*);
using GroupChildFn = int(__fastcall*)(void*, int, void**);
using GetDspFn = int(__fastcall*)(void*, int, void**);
using AddInputFn = int(__fastcall*)(void*, void*, void**, int);
using IsPlayingFn = int(__fastcall*)(void*, bool*);

const FH2ModApi* g_api = nullptr;
FH2Mod* g_mod = nullptr;
std::uint8_t* g_base = nullptr;
StudioUpdateFn g_original_update = nullptr;

std::vector<std::wstring> g_files;
std::vector<std::vector<char>> g_data;
std::vector<void*> g_sounds;
std::vector<unsigned> g_loop_start, g_loop_end;
void* g_core = nullptr;
std::atomic<bool> g_ready{false};
std::atomic<int> g_request{-1};
std::atomic<float> g_volume{1.0f};
int g_playing = -1;
void* g_channel = nullptr;
bool g_logged_play_failure = false;
std::uint64_t g_last_log = 0;

void logf(const char* fmt, ...) {
    char buf[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (g_api && g_mod) g_api->log(g_mod, buf);
}

template <typename Fn>
Fn fn(std::uintptr_t rva) {
    return reinterpret_cast<Fn>(g_base + rva);
}

int safe_low_level(void* studio, void** core) {
    __try {
        return fn<GetLowLevelFn>(kStudioGetLowLevel)(studio, core);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1000;
    }
}

int safe_listener(Vec* pos, Vec* vel, Vec* fwd, Vec* up) {
    __try {
        return fn<GetListenerFn>(kGetListener)(g_core, 0, pos, vel, fwd, up);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1000;
    }
}

int safe_create(int index) {
    __try {
        unsigned char info[kExInfoSize] = {};
        *reinterpret_cast<unsigned*>(info) = kExInfoSize;
        *reinterpret_cast<unsigned*>(info + 4) =
            static_cast<unsigned>(g_data[index].size());
        void* sound = nullptr;
        const int r = fn<CreateSoundFn>(kCreateSound)(
            g_core, g_data[index].data(),
            kModeOpenMemory | kModeCreateSample | kMode3D | kModeLoop |
                kModeWorldRelative | kModeInverseRolloff,
            info, &sound);
        if (r == 0) {
            g_sounds[index] = sound;
            if (g_loop_end[index] > g_loop_start[index])
                fn<LoopPointsFn>(kSetLoopPoints)(sound, g_loop_start[index], 2,
                                                 g_loop_end[index] - 1, 2);
        }
        return r;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1000;
    }
}

int safe_play(int index, const Vec& pos, const Vec& vel) {
    __try {
        void* channel = nullptr;
        int r = fn<PlaySoundFn>(kPlaySound)(g_core, g_sounds[index], nullptr,
                                            true, &channel);
        if (r != 0 || !channel) return r ? r : -1001;
        fn<Set3DFn>(kSet3DAttributes)(channel, &pos, &vel, nullptr);
        fn<SetVolumeFn>(kSetVolume)(channel, g_volume.load());
        fn<MinMaxFn>(kSet3DMinMax)(channel, 5.0f, 150.0f);
        fn<SetPausedFn>(kSetPaused)(channel, false);
        g_channel = channel;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_channel = nullptr;
        return -1000;
    }
}

void safe_move(const Vec& pos, const Vec& vel) {
    __try {
        fn<Set3DFn>(kSet3DAttributes)(g_channel, &pos, &vel, nullptr);
        fn<SetVolumeFn>(kSetVolume)(g_channel, g_volume.load());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_channel = nullptr;
    }
}

void safe_stop() {
    __try {
        if (g_channel) fn<StopFn>(kStop)(g_channel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_channel = nullptr;
}

// Reads the FSB5 bank (mono PCM16) and wraps every sample as an in-memory WAV.
bool load_bank(const std::filesystem::path& path, int count) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::vector<char> f((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (f.size() < 60 || std::memcmp(f.data(), "FSB5", 4) != 0) return false;
    std::uint32_t h[6];
    std::memcpy(h, f.data() + 4, sizeof(h));
    const std::uint32_t n = h[1], shs = h[2], nts = h[3], ds = h[4], mode = h[5];
    if (mode != 2 || n != static_cast<std::uint32_t>(count) ||
        60ull + shs + nts + ds > f.size())
        return false;
    const char* data = f.data() + 60 + shs + nts;
    std::size_t p = 60;
    g_loop_start.assign(n, 0);
    g_loop_end.assign(n, 0);
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint64_t v;
        if (p + 8 > 60ull + shs) return false;
        std::memcpy(&v, f.data() + p, 8);
        p += 8;
        std::uint32_t more = v & 1;
        while (more) {
            std::uint32_t c;
            if (p + 4 > 60ull + shs) return false;
            std::memcpy(&c, f.data() + p, 4);
            p += 4;
            more = c & 1;
            const std::uint32_t size = (c >> 1) & 0xFFFFFF, type = c >> 25;
            if (p + size > 60ull + shs) return false;
            if (type == 3 && size >= 8) {
                std::memcpy(&g_loop_start[i], f.data() + p, 4);
                std::memcpy(&g_loop_end[i], f.data() + p + 4, 4);
            }
            p += size;
        }
        const std::uint64_t off = ((v >> 7) & 0x7FFFFFF) * 32;
        const std::uint64_t frames = v >> 34;
        if (off + frames * 2 > ds) return false;
        if (g_loop_end[i] > frames) g_loop_end[i] = 0;
        const std::uint32_t bytes = static_cast<std::uint32_t>(frames * 2);
        std::vector<char>& w = g_data[i];
        w.resize(44 + bytes);
        const std::uint32_t riff = 36 + bytes, fmtSize = 16, rate = 44100,
                            byteRate = rate * 2;
        const std::uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
        char* q = w.data();
        std::memcpy(q, "RIFF", 4);
        std::memcpy(q + 4, &riff, 4);
        std::memcpy(q + 8, "WAVEfmt ", 8);
        std::memcpy(q + 16, &fmtSize, 4);
        std::memcpy(q + 20, &pcm, 2);
        std::memcpy(q + 22, &ch, 2);
        std::memcpy(q + 24, &rate, 4);
        std::memcpy(q + 28, &byteRate, 4);
        std::memcpy(q + 32, &align, 2);
        std::memcpy(q + 34, &bits, 2);
        std::memcpy(q + 36, "data", 4);
        std::memcpy(q + 40, &bytes, 4);
        std::memcpy(q + 44, data + off, bytes);
    }
    return true;
}

bool load_file(int index) {
    return index >= 0 && index < static_cast<int>(g_data.size()) &&
           !g_data[index].empty();
}

// Probe placement: a fixed spot ahead of the camera, so the stereo image
// proves that the engine is positioning the sound against its own listener.
std::atomic<const void*> g_car{nullptr};
std::atomic<std::uint64_t> g_car_tick{0};

bool car_position(Vec* out) {
    const auto* car = static_cast<const std::uint8_t*>(g_car.load());
    if (!car || GetTickCount64() - g_car_tick.load() > 1000) return false;
    __try {
        const auto* state = *reinterpret_cast<const std::uint8_t* const*>(car + 0x4C08);
        if (!state) return false;
        const float* p = reinterpret_cast<const float*>(state + 0xEF0);
        out->x = p[0];
        out->y = p[1];
        out->z = p[2];
        return out->x == out->x && out->y == out->y && out->z == out->z;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void hud_hook(FH2Context* ctx, void*) {
    const void* hud = reinterpret_cast<const void*>(ctx->rdi);
    __try {
        g_car.store(*reinterpret_cast<void* const*>(
            static_cast<const std::uint8_t*>(hud) + 0xE8));
        g_car_tick.store(GetTickCount64());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// vel is the listener velocity, so the horn has no relative speed (no Doppler).
bool probe_position(Vec* out, Vec* vel_out) {
    Vec pos{}, vel{}, fwd{}, up{};
    if (safe_listener(&pos, &vel, &fwd, &up) != 0) return false;
    *vel_out = vel;
    if (car_position(out)) return true;
    out->x = pos.x + fwd.x * 15.0f;
    out->y = pos.y + fwd.y * 15.0f;
    out->z = pos.z + fwd.z * 15.0f;
    return true;
}

void tick() {
    if (!g_core) return;
    const int want = g_request.load(std::memory_order_relaxed);
    if (want != g_playing) {
        safe_stop();
        g_playing = -1;
        if (want >= 0 && want < static_cast<int>(g_files.size())) {
            g_playing = want;
            if (!load_file(want)) {
                if (!g_logged_play_failure)
                    logf("FMOD horn: could not read horn file %d.", want);
                g_logged_play_failure = true;
                return;
            }
            if (!g_sounds[want]) {
                const int r = safe_create(want);
                if (r != 0) {
                    logf("FMOD horn: createSound failed (%d).", r);
                    return;
                }
            }
            Vec pos{}, vel{};
            if (!probe_position(&pos, &vel)) {
                logf("FMOD horn: could not read the listener.");
                return;
            }
            const int r = safe_play(want, pos, vel);
            if (r != 0) {
                logf("FMOD horn: playSound failed (%d).", r);
            } else {
                logf("FMOD horn: playing horn %d at %.1f %.1f %.1f.", want,
                     pos.x, pos.y, pos.z);
            }
        }
    } else if (g_channel) {
        Vec pos{}, vel{};
        if (probe_position(&pos, &vel)) safe_move(pos, vel);
    }
}

int __fastcall detour_update(void* studio) {
    const int result = g_original_update ? g_original_update(studio) : 0;
    if (!g_core && studio) {
        void* core = nullptr;
        if (safe_low_level(studio, &core) == 0 && core) {
            g_core = core;
            g_ready.store(true, std::memory_order_release);
            logf("FMOD horn: engine captured (studio %p, core %p).", studio,
                 core);
        }
    }
    tick();
    const std::uint64_t now = GetTickCount64();
    if (g_core && now - g_last_log > 10000) {
        g_last_log = now;
        Vec pos{}, vel{}, fwd{}, up{};
        if (safe_listener(&pos, &vel, &fwd, &up) == 0) {
            logf("FMOD horn: listener at %.1f %.1f %.1f facing %.2f %.2f %.2f.",
                 pos.x, pos.y, pos.z, fwd.x, fwd.y, fwd.z);
        }
    }
    return result;
}

} // namespace

bool install(const FH2ModApi* api, FH2Mod* mod, void* game_base,
             const std::filesystem::path& bank_path, int count) {
    g_api = api;
    g_mod = mod;
    g_base = static_cast<std::uint8_t*>(game_base);
    g_data.assign(count, {});
    g_sounds.assign(count, nullptr);
    g_files.assign(count, std::wstring());
    if (!load_bank(bank_path, count)) {
        logf("FMOD horn: could not load the horn bank HZ2_SFX_HonkHonk.bank.");
        return false;
    }
    if (!api->hook_function || !api->enable_hooks) return false;
    FH2Hook* hook = api->hook_function(
        mod, "HonkHonk: FMOD Studio::System::update", g_base + kStudioUpdate,
        reinterpret_cast<void*>(detour_update),
        reinterpret_cast<void**>(&g_original_update));
    if (!hook || !api->enable_hooks(mod, hook)) {
        logf("FMOD horn: could not hook Studio::System::update.");
        return false;
    }
    if (api->api_version >= 7 && api->hook_instruction) {
        FH2Hook* h = api->hook_instruction(mod, "HonkHonk: car position",
                                           g_base + 0x783E67, hud_hook, nullptr);
        if (!h || !api->enable_hooks(mod, h))
            logf("FMOD horn: car position hook failed; using the camera.");
    }
    logf("FMOD horn: hook installed.");
    return true;
}

bool ready() { return g_ready.load(std::memory_order_acquire); }

void set_volume(float v) { g_volume.store(v); }

void request(int index) { g_request.store(index, std::memory_order_relaxed); }

} // namespace fmodhorn
