// Include the implementation to exercise private memory readers and hook wiring
// with a fake game image, without exposing test-only APIs in the mod.
#include "../HonkHonk.cpp"
#include "../FmodHorn.cpp"

#include <iostream>
#include <stdexcept>

namespace {

int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

template <typename T>
void write_at(std::uint8_t* base, std::size_t offset, T value) {
    std::memcpy(base + offset, &value, sizeof(value));
}

std::uint8_t* image = nullptr;
std::vector<std::uintptr_t> hook_targets;
std::vector<std::string> messages;
bool fail_hud = false;
bool fail_dynamics = false;
FH2Hook* disabled_hook = nullptr;

void mock_log(FH2Mod*, const char* message) { messages.emplace_back(message); }
void* mock_wait(std::uint32_t) { return image; }
FH2Hook* mock_function(FH2Mod*, const char*, void* target, void*, void**) {
    hook_targets.push_back(static_cast<std::uint8_t*>(target) - image);
    return reinterpret_cast<FH2Hook*>(target);
}
FH2Hook* mock_instruction(FH2Mod*, const char*, void* target, FH2InstructionFn, void*) {
    hook_targets.push_back(static_cast<std::uint8_t*>(target) - image);
    return fail_hud ? nullptr : reinterpret_cast<FH2Hook*>(target);
}
int mock_enable(FH2Mod*, FH2Hook* hook) {
    return !(fail_dynamics && hook == reinterpret_cast<FH2Hook*>(image + 0x12C1EA0));
}
int mock_disable(FH2Mod*, FH2Hook* hook) {
    disabled_hook = hook;
    return 1;
}

void test_input(std::vector<std::uint8_t>& manager) {
    std::uint16_t buttons = 0;
    g_game_image.store(nullptr);
    check(!game_pad_buttons(&buttons), "Input before game mapping");
    g_game_image.store(image);
    check(!game_pad_buttons(&buttons), "Missing input manager");
    write_at(image, 0x365D7C8, manager.data());
    check(!game_pad_buttons(&buttons), "Wrong manager vtable");
    write_at(manager.data(), 0, image + 0x2E7A2E8);
    check(!game_pad_buttons(&buttons), "No active controller");
    for (int i = 0; i < 8; ++i) {
        auto* controller = manager.data() + 0x10 + i * 0x1300;
        write_at(manager.data(), 0xAB28, controller);
        controller[4] = 0;
        check(!game_pad_buttons(&buttons), "Disconnected controller");
        controller[4] = 1;
        write_at<std::uint16_t>(controller, 0xC0, kHornButton);
        check(game_pad_buttons(&buttons) && buttons == kHornButton, "Connected pad L3");
        write_at<std::uint16_t>(controller, 0xC0, 0x0080);
        check(game_pad_buttons(&buttons) && (buttons & kHornButton) == 0, "Other button");
        write_at<std::uint16_t>(controller, 0xC0, 0);
        check(game_pad_buttons(&buttons) && buttons == 0, "Released button");
    }
    write_at(manager.data(), 0xAB28, manager.data() + 0x11);
    check(!game_pad_buttons(&buttons), "Misaligned slot");
    write_at(manager.data(), 0xAB28, manager.data());
    check(!game_pad_buttons(&buttons), "Pointer below slots");
    write_at(manager.data(), 0xAB28, manager.data() + 0x9810);
    check(!game_pad_buttons(&buttons), "Non-pad slot excluded");
    write_at(image, 0x365D7C8, reinterpret_cast<void*>(1));
    check(!game_pad_buttons(&buttons), "Unreadable input manager");
}

void test_ticks() {
    fmodhorn::g_hud_hooked.store(true);
    g_last_physics_tick.store(0);
    fmodhorn::g_car_tick.store(1000);
    check(!gameplay_active(1000), "No physics tick");
    g_last_physics_tick.store(1000);
    fmodhorn::g_car_tick.store(0);
    check(!gameplay_active(1000), "No HUD tick");
    fmodhorn::g_car_tick.store(1000);
    check(gameplay_active(1000), "Both ticks fresh");
    check(gameplay_active(1249), "249 ms is active");
    check(!gameplay_active(1250), "250 ms expires");
    g_last_physics_tick.store(1250);
    check(!gameplay_active(1250), "Stale HUD despite fresh physics");
    fmodhorn::g_car_tick.store(1250);
    g_last_physics_tick.store(1000);
    check(!gameplay_active(1250), "Stale physics despite fresh HUD");
    fmodhorn::g_hud_hooked.store(false);
    g_last_physics_tick.store(1250);
    fmodhorn::g_car_tick.store(0);
    check(gameplay_active(1250), "Physics-only fallback");
    check(!gameplay_active(1500), "Physics-only fallback expires");

    alignas(void*) std::uint8_t hud[0xF0] = {};
    FH2Context ctx{};
    ctx.rdi = reinterpret_cast<std::uint64_t>(hud);
    write_at(hud, 0xE8, hud);
    fmodhorn::hud_hook(&ctx, nullptr);
    check(fmodhorn::hud_tick() != 0 && fmodhorn::g_car.load() == hud, "HUD captures car");
    write_at<const void*>(hud, 0xE8, nullptr);
    fmodhorn::hud_hook(&ctx, nullptr);
    check(fmodhorn::hud_tick() == 0 && !fmodhorn::g_car.load(), "HUD clears missing car");
    fmodhorn::g_car_tick.store(1000);
    ctx.rdi = 0;
    fmodhorn::hud_hook(&ctx, nullptr);
    check(fmodhorn::hud_tick() == 0, "Unreadable HUD clears tick");
}

struct ChoiceRow {
    std::string key;
    std::vector<std::string> names;
    int value;
    FH2SettingChangedFn on_change;
};
std::vector<ChoiceRow> choice_rows;
unsigned mock_choice_limit = 32;
int mock_add_setting(FH2Mod*, const FH2Setting* setting) {
    if (setting->kind != FH2_SETTING_CHOICE || setting->choice_count < 2 ||
        setting->choice_count > mock_choice_limit) return 0;
    ChoiceRow row{setting->key, {}, setting->value, setting->on_change};
    for (unsigned i = 0; i < setting->choice_count; ++i)
        row.names.emplace_back(setting->choices[i]);
    choice_rows.push_back(row);
    return 1;
}
void mock_set_setting(FH2Mod*, const char* key, int value) {
    for (auto& row : choice_rows) if (row.key == key) row.value = value;
}

void test_selector() {
    FH2ModApi api{};
    api.api_version = 9;
    api.add_setting = mock_add_setting;
    api.set_setting = mock_set_setting;
    api.log = mock_log;
    g_api = &api;
    int handle = 0;
    g_mod = reinterpret_cast<FH2Mod*>(&handle);
    g_ini_path = std::filesystem::temp_directory_path() /
        (L"HonkHonk-selector-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    struct Cleanup {
        ~Cleanup() { std::filesystem::remove(g_ini_path); }
    } cleanup;
    // Simulate API 9/10's real 32-choice limit, modern 100-choice hosts,
    // and a modern host that refuses the long row and needs the fallback.
    for (int mode = 0; mode < 5; ++mode) {
        api.api_version = mode == 0 ? 9 : mode == 1 ? 10 : mode == 3 ? 12 : 11;
        mock_choice_limit = mode < 2 || mode == 4 ? 32 : 100;
        const bool single = mode == 2 || mode == 3;
        for (int initial : {0, 7, 10, 11, 30, 31, 46}) {
            choice_rows.clear();
            g_selected_horn.store(initial);
            check(add_horn_choice_settings(&api, g_mod), "Selectors accepted by host limit");
            check(g_single_horn_selector == single, "Correct layout chosen for host");
            check(choice_rows.size() == (single ? 1 : 2), "Expected number of named rows");
            if (single) {
                check(choice_rows[0].names.size() == kHornDefinitions.size(), "Single row contains all sounds");
                check(choice_rows[0].value == initial, "Single row restores saved bank index");
            } else {
                check(choice_rows[0].names.size() == 32, "Legacy first row fits old platform");
                for (int row = 0; row < 2; ++row) {
                    const int offset = row * kHornsPerChoiceRow;
                    check(choice_rows[row].value == (initial >= offset &&
                        initial < offset + static_cast<int>(choice_rows[row].names.size()) - 1
                        ? initial - offset + 1 : 0), "Saved index restored in correct legacy row");
                }
            }
        }
        for (int selected = 0; selected < static_cast<int>(kHornDefinitions.size()); ++selected) {
            const int row = single ? 0 : selected / kHornsPerChoiceRow;
            const int local = single ? selected : selected % kHornsPerChoiceRow + 1;
            auto& entry = choice_rows[row];
            check(entry.names[local] == kHornDefinitions[selected].name, "Choice name matches bank index");
            entry.on_change(nullptr, entry.key.c_str(), local);
            check(g_selected_horn.load() == selected, "Every choice selects correct audio");
            check(entry.value == local, "Selected row synchronized");
            if (!single) check(choice_rows[1-row].value == 0, "Other row marked inactive");
            check(GetPrivateProfileIntW(kIniSection, kSelectedHornKey, -1, g_ini_path.c_str()) == selected,
                  "Selection saved without changing bank indexes");
        }
        const int last = static_cast<int>(kHornDefinitions.size()) - 1;
        for (auto& entry : choice_rows) {
            entry.on_change(nullptr, entry.key.c_str(), -1);
            check(g_selected_horn.load() == last, "Negative value ignored");
            entry.on_change(nullptr, entry.key.c_str(), static_cast<int>(entry.names.size()));
            check(g_selected_horn.load() == last, "Out of range choice ignored");
            if (!single) {
                entry.on_change(nullptr, entry.key.c_str(), 0);
                check(g_selected_horn.load() == last, "Inactive indicator preserves selection");
            }
        }
        on_horn_choice_changed(nullptr, "InvalidKey", 1);
        check(g_selected_horn.load() == last, "Unknown selector key ignored");
        on_horn_choice_changed(nullptr, nullptr, 1);
        check(g_selected_horn.load() == last, "Null selector key ignored");
        if (single) {
            on_horn_choice_changed(nullptr, kSettingHornSoundExtra, 1);
            check(g_selected_horn.load() == last, "Legacy extra row key ignored in single mode");
        }
    }
    g_api = nullptr;
    g_mod = nullptr;
}

void test_hooks(const std::filesystem::path& bank_directory) {
    FH2ModApi api{};
    api.api_version = 9;
    api.log = mock_log;
    api.wait_for_code = mock_wait;
    api.hook_function = mock_function;
    api.hook_instruction = mock_instruction;
    api.enable_hooks = mock_enable;
    api.disable_hooks = mock_disable;
    g_api = &api;
    int handle = 0;
    g_mod = reinterpret_cast<FH2Mod*>(&handle);
    g_mod_directory = bank_directory;
    for (int mode = 0; mode < 4; ++mode) {
        hook_targets.clear();
        messages.clear();
        disabled_hook = nullptr;
        fail_hud = mode == 1;
        fail_dynamics = mode == 3;
        api.hook_instruction = mode == 2 ? nullptr : mock_instruction;
        fmodhorn::g_hud_hooked.store(false);
        install_gameplay_hooks();
        check(std::find(hook_targets.begin(), hook_targets.end(), 0xAD6070) == hook_targets.end(),
              "UpdateGauges must never be hooked");
        check(std::find(hook_targets.begin(), hook_targets.end(), 0x12C1EA0) != hook_targets.end(),
              "Physics hook registered");
        check(std::find(hook_targets.begin(), hook_targets.end(), 0x1A9C7B0) != hook_targets.end(),
              "FMOD hook registered");
        check(fmodhorn::hud_hooked() == (mode == 0 || mode == 3), "HUD hook state");
        if (mode == 1 || mode == 2) {
            check(std::any_of(messages.begin(), messages.end(), [](const std::string& s) {
                return s.find("physics-only") != std::string::npos;
            }), "Fallback is logged");
        }
        if (mode == 3) {
            check(disabled_hook == reinterpret_cast<FH2Hook*>(image + 0x12C1EA0),
                  "Only failed dynamics hook disabled");
            check(fmodhorn::hud_hooked(), "HUD hook not disabled by dynamics failure");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Pass the folder containing HonkHonk's bank folder.");
        std::vector<std::uint8_t> game_image(0x365D7D0);
        std::vector<std::uint8_t> manager(0xAB30);
        image = game_image.data();
        test_input(manager);
        test_ticks();
        test_selector();
        test_hooks(std::filesystem::path(argv[1]));
        std::cout << "Passed " << checks << " gameplay/input/hook checks.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
