#include "FmodHorn.h"
#include "fh2mod.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

constexpr char kSettingEnabled[] = "HornEnabled";
constexpr char kSettingHornSound[] = "HornSound";
constexpr wchar_t kBankName[] = L"HZ2_SFX_HonkHonk.bank";
constexpr wchar_t kIniName[] = L"HonkHonk.ini";
constexpr wchar_t kIniSection[] = L"HonkHonk";
constexpr wchar_t kEnabledKey[] = L"HornEnabled";
constexpr wchar_t kSelectedHornKey[] = L"SelectedHorn";
constexpr DWORD kGameplayTickTimeoutMs = 250;
constexpr DWORD kControllerPollMs = 16;
// The horn button as the game reports it (the XInput button mask): the left stick click, XINPUT_GAMEPAD_LEFT_THUMB.
constexpr unsigned short kHornButton = 0x0040;
struct HornDefinition {
    const wchar_t* file;
    const char* name;
};
constexpr std::array<HornDefinition, 31> kHornDefinitions = {{
    {L"Car.wav", "Car"},
    {L"Truck.wav", "Truck"},
    {L"Classic.wav", "Classic"},
    {L"Train.wav", "Train"},
    {L"Warthog.wav", "Warthog"},
    {L"Bicycle.wav", "Bicycle"},
    {L"Reversing.wav", "Reversing"},
    {L"Police.wav", "Police"},
    {L"Boat.wav", "Boat"},
    {L"Doom.wav", "Doom"},
    {L"Doorbell.wav", "Doorbell"},
    {L"Honk.wav", "Honk"},
    {L"Horse.wav", "Horse"},
    {L"Quack.wav", "Quack"},
    {L"Rooster.wav", "Rooster"},
    {L"Starter.wav", "Starter"},
    {L"Teams.wav", "Teams"},
    {L"WindowsXP.wav", "WindowsXP"},
    {L"Wololo.wav", "Wololo"},
    {L"Augh.wav", "Augh"},
    {L"Bad to the bone.wav", "Bad to the bone"},
    {L"Bruh.wav", "Bruh"},
    {L"DejaVu.wav", "DejaVu"},
    {L"Discord.wav", "Discord"},
    {L"DJ.wav", "DJ"},
    {L"Goofy.wav", "Goofy"},
    {L"Lego.wav", "Lego"},
    {L"MGS.wav", "MGS"},
    {L"Social credit.wav", "Social credit"},
    {L"Spiderman.wav", "Spiderman"},
    {L"Wow.wav", "Wow"},
}};

const FH2ModApi* g_api = nullptr;
FH2Mod* g_mod = nullptr;
std::filesystem::path g_mod_directory;
std::filesystem::path g_ini_path;
std::atomic<bool> g_enabled{true};
std::atomic<int> g_selected_horn{0};
std::atomic<std::uint64_t> g_last_physics_tick{0};

using CarDynamicsUpdateFn =
    bool(__fastcall*)(void*, void*, void*, std::uint32_t, float);
CarDynamicsUpdateFn g_original_car_dynamics_update = nullptr;

void log_message(const char* message) {
    if (g_api && g_mod && g_api->log) {
        g_api->log(g_mod, message);
    }
}

std::filesystem::path module_directory() {
    std::wstring module_path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        reinterpret_cast<HMODULE>(&__ImageBase), module_path.data(),
        static_cast<DWORD>(module_path.size()));
    if (length == 0 || length >= module_path.size()) {
        return {};
    }
    module_path.resize(length);
    return std::filesystem::path(module_path).parent_path();
}

void save_config_value(const wchar_t* key, const wchar_t* value) {
    if (!WritePrivateProfileStringW(
            kIniSection, key, value, g_ini_path.c_str())) {
        log_message("Failed to save HonkHonk.ini.");
    }
}

void save_selected_horn(int selected) {
    save_config_value(kSelectedHornKey, std::to_wstring(selected).c_str());
}

void select_horn(int selected) {
    if (selected < 0 ||
        selected >= static_cast<int>(kHornDefinitions.size())) {
        log_message("Ignored invalid horn selection.");
        return;
    }
    g_selected_horn.store(selected, std::memory_order_relaxed);
    save_selected_horn(selected);
    if (g_api && g_api->api_version >= 5 && g_mod && g_api->set_slider) {
        g_api->set_slider(g_mod, kSettingHornSound,
                          static_cast<float>(selected));
    }
}

void on_enabled_changed(void*, const char* key, int value) {
    if (!key || std::strcmp(key, kSettingEnabled) != 0 ||
        (value != 0 && value != 1)) {
        log_message("Ignored invalid HornEnabled menu setting.");
        return;
    }

    g_enabled.store(value == 1, std::memory_order_relaxed);
    save_config_value(kEnabledKey, value == 1 ? L"1" : L"0");
}

void on_horn_sound_changed(void*, const char* key, float value) {
    if (!key || std::strcmp(key, kSettingHornSound) != 0 ||
        !std::isfinite(value) || value < 0.0f ||
        value > static_cast<float>(kHornDefinitions.size() - 1)) {
        log_message("Ignored invalid HornSound menu setting.");
        return;
    }
    select_horn(static_cast<int>(value + 0.5f));
}

bool __fastcall detour_car_dynamics_update(
    void* self, void* second, void* third, std::uint32_t fourth, float delta_time) {
    const bool result = g_original_car_dynamics_update
        ? g_original_car_dynamics_update(self, second, third, fourth, delta_time)
        : false;
    if (self) {
        g_last_physics_tick.store(GetTickCount64(), std::memory_order_relaxed);
    }
    return result;
}

// The game's active controller, read from its own input state like Extra Options does, so no XInputGetState polling.
// XInputGetState costs 0.5-0.8 ms per call under Wine/Proton (even for empty slots); four slots every 16 ms was a sixth of a core and
// constant wineserver traffic, and lagged full lobbies. Layout: input manager at game+0x365D7C8 (vtable game+0x2E7A2E8), active controller
// pointer at manager+0xAB28 inside the array of 8 controllers of 0x1300 bytes from manager+0x10, connected byte at +4, button mask at +0xC0.
std::atomic<std::uint8_t*> g_game_image{nullptr};

bool game_pad_buttons(unsigned short* buttons) {
    const std::uint8_t* image = g_game_image.load(std::memory_order_relaxed);
    if (!image) return false;
    __try {
        const auto* manager =
            *reinterpret_cast<const std::uint8_t* const*>(image + 0x365D7C8);
        if (!manager ||
            *reinterpret_cast<const void* const*>(manager) != image + 0x2E7A2E8) {
            return false;
        }
        const auto* controller =
            *reinterpret_cast<const std::uint8_t* const*>(manager + 0xAB28);
        const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(manager) + 0x10;
        const std::uintptr_t selected = reinterpret_cast<std::uintptr_t>(controller);
        if (selected < first || selected >= first + 8 * 0x1300 ||
            (selected - first) % 0x1300 != 0 || controller[4] != 1) {
            return false;
        }
        *buttons = *reinterpret_cast<const std::uint16_t*>(controller + 0xC0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool gameplay_active(std::uint64_t now) {
    const std::uint64_t physics_tick =
        g_last_physics_tick.load(std::memory_order_relaxed);
    if (physics_tick == 0 || now - physics_tick >= kGameplayTickTimeoutMs) {
        return false;
    }
    // The speedometer's update, from the car position hook in HUDSpeedometerComponent::Update. Without that hook only the physics tick counts.
    if (!fmodhorn::hud_hooked()) {
        return true;
    }
    const std::uint64_t speedometer_tick = fmodhorn::hud_tick();
    return speedometer_tick != 0 && now - speedometer_tick < kGameplayTickTimeoutMs;
}

void install_gameplay_hooks() {
    if (!g_api || g_api->api_version < 3 || !g_api->wait_for_code ||
        !g_api->hook_function || !g_api->enable_hooks ||
        !g_api->disable_hooks) {
        log_message("The platform does not provide the gameplay-hook API; horn input is unavailable.");
        return;
    }

    void* game_base = g_api->wait_for_code(30000);
    if (!game_base) {
        log_message("Timed out waiting for the game code; horn input is unavailable.");
        return;
    }

    auto* base = static_cast<std::uint8_t*>(game_base);
    g_game_image.store(base, std::memory_order_relaxed);
    fmodhorn::install(g_api, g_mod, game_base,
                      g_mod_directory / L"HonkHonk" / kBankName,
                      static_cast<int>(kHornDefinitions.size()));
    FH2Hook* dynamics_hook = g_api->hook_function(
        g_mod, "HonkHonk: CCarDynamics::UpdateDynamics",
        base + 0x12C1EA0,
        reinterpret_cast<void*>(detour_car_dynamics_update),
        reinterpret_cast<void**>(&g_original_car_dynamics_update));
    // No hook on HUDStatusSpeedometer::UpdateGauges (game+0xAD6070): on Windows its detour ended in a jump to freed memory on the first
    // speedometer update (crash reports from 2026-10-30 on, also with NitroSystem's hook there). The car position hook at game+0x783E67 runs
    // in the same HUDSpeedometerComponent::Update just before that call and gives the same tick (fmodhorn::hud_tick).

    if (!dynamics_hook) {
        log_message("Could not create the gameplay-detection hook; horn input is disabled.");
        return;
    }
    if (!g_api->enable_hooks(g_mod, dynamics_hook)) {
        g_api->disable_hooks(g_mod, nullptr);
        log_message("Could not enable the gameplay-detection hook; horn input is disabled.");
        return;
    }

    log_message("Gameplay-only horn input hooks are active.");
}

void horn_worker() {
    install_gameplay_hooks();

    for (;;) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kControllerPollMs));

        unsigned short buttons = 0;
        const bool horn_held = game_pad_buttons(&buttons) && (buttons & kHornButton) != 0;

        const bool should_play =
            horn_held && g_enabled.load(std::memory_order_relaxed) &&
            gameplay_active(GetTickCount64());
        fmodhorn::request(
            should_play ? g_selected_horn.load(std::memory_order_relaxed) : -1);
    }
}

} // namespace

FH2MOD_EXPORT int fh2mod_init(const FH2ModApi* api) {
    if (!api || api->api_version < 2 || !api->register_mod ||
        !api->clear_file || !api->log) {
        return 1;
    }

    FH2ModInfo info{};
    info.size = sizeof(info);
    info.name = "HonkHonk";
    info.version = "0.3.3";
    info.github_repo = "suderek22/FH2_HonkHonk";
    info.platform_version = "2.7.1";
    info.check_updates = 1;
    info.description =
        "Hold the left stick while driving to play the selected horn.";

    g_api = api;
    g_mod = api->register_mod(&info);
    if (!g_mod) {
        return 1;
    }

    g_mod_directory = module_directory();
    if (g_mod_directory.empty()) {
        log_message("Could not locate HonkHonk.dll; the mod was not initialized.");
        return 1;
    }
    g_ini_path = g_mod_directory / kIniName;

    if (!CreateDirectoryW((g_mod_directory / L"HonkHonk").c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        log_message("Could not create the HonkHonk sound directory.");
        return 1;
    }

    const std::filesystem::path legacy_ini_path =
        g_mod_directory / L"HonkHorn.ini";
    const bool has_new_ini =
        GetFileAttributesW(g_ini_path.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool has_legacy_ini =
        GetFileAttributesW(legacy_ini_path.c_str()) != INVALID_FILE_ATTRIBUTES;
    const wchar_t* read_section = kIniSection;
    std::filesystem::path read_ini_path = g_ini_path;
    if (!has_new_ini && has_legacy_ini) {
        read_ini_path = legacy_ini_path;
        read_section = L"HonkHorn";
        log_message("Migrating settings from HonkHorn.ini.");
    }

    const int enabled_value = GetPrivateProfileIntW(
        read_section, kEnabledKey, 1, read_ini_path.c_str());
    const int selected_value = GetPrivateProfileIntW(
        read_section, kSelectedHornKey, 0, read_ini_path.c_str());
    if ((enabled_value != 0 && enabled_value != 1) ||
        selected_value < 0 ||
        selected_value >= static_cast<int>(kHornDefinitions.size())) {
        log_message("HonkHonk.ini contains an invalid option; using enabled Car defaults.");
        g_enabled.store(true, std::memory_order_relaxed);
        g_selected_horn.store(0, std::memory_order_relaxed);
    } else {
        g_enabled.store(enabled_value == 1, std::memory_order_relaxed);
        g_selected_horn.store(selected_value, std::memory_order_relaxed);
    }
    {
        wchar_t buf[32] = {};
        GetPrivateProfileStringW(read_section, L"HornVolume", L"1.0", buf, 32,
                                 read_ini_path.c_str());
        float volume = static_cast<float>(_wtof(buf));
        if (!(volume >= 0.0f && volume <= 4.0f)) volume = 1.0f;
        fmodhorn::set_volume(volume);
        if (GetPrivateProfileStringW(kIniSection, L"HornVolume", L"", buf, 32,
                                     g_ini_path.c_str()) == 0)
            save_config_value(L"HornVolume", L"1.0");
    }
    save_config_value(
        kEnabledKey, g_enabled.load(std::memory_order_relaxed) ? L"1" : L"0");
    save_selected_horn(g_selected_horn.load(std::memory_order_relaxed));

    if (api->api_version >= 4 && api->add_setting) {
        FH2Setting setting{};
        setting.size = sizeof(setting);
        setting.key = kSettingEnabled;
        setting.label = "Horn";
        setting.description =
            "Enable or disable the left stick horn while driving.";
        setting.value = g_enabled.load(std::memory_order_relaxed) ? 1 : 0;
        setting.needs_restart = 0;
        setting.on_change = on_enabled_changed;
        if (!api->add_setting(g_mod, &setting)) {
            log_message("Could not add the Horn on/off option to the Mods menu.");
        }
    }

    if (api->api_version >= 5 && api->add_setting) {
        FH2Setting setting{};
        setting.size = sizeof(setting);
        setting.key = kSettingHornSound;
        setting.label = "Horn sound";
        setting.description =
            "Slider values: 0 = Car, 1 = Truck, 2 = Classic, 3 = Train, "
            "4 = Warthog, 5 = Bicycle, 6 = Reversing, 7 = Police, "
            "8 = Boat, 9 = Doom, 10 = Doorbell, 11 = Honk, 12 = Horse, "
            "13 = Quack, 14 = Rooster, 15 = Starter, 16 = Teams, "
            "17 = WindowsXP, 18 = Wololo, 19 = Augh, 20 = Bad to the bone, "
            "21 = Bruh, 22 = DejaVu, 23 = Discord, 24 = DJ, 25 = Goofy, "
            "26 = Lego, 27 = MGS, 28 = Social credit, 29 = Spiderman, "
            "30 = Wow.";
        setting.kind = FH2_SETTING_SLIDER;
        setting.min = 0.0f;
        setting.max = static_cast<float>(kHornDefinitions.size() - 1);
        setting.step = 1.0f;
        setting.slider_value = static_cast<float>(
            g_selected_horn.load(std::memory_order_relaxed));
        setting.decimals = 0;
        setting.on_slide = on_horn_sound_changed;
        if (!api->add_setting(g_mod, &setting)) {
            log_message("Could not add the Horn sound slider to the Mods menu.");
        }
    } else {
        log_message("The platform slider API is unavailable; use A on Horn sound to switch horns.");
    }

    try {
        std::thread(horn_worker).detach();
    } catch (...) {
        log_message("Could not start the horn input worker thread.");
        return 1;
    }

    log_message("HonkHonk initialized; hold left stick to honk during gameplay.");
    return 0;
}
