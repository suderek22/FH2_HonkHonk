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
        test_hooks(std::filesystem::path(argv[1]));
        std::cout << "Passed " << checks << " gameplay/input/hook checks.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
