#include "HonkHonkAudio.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Pass at least one WAV file to validate.\n";
        return 2;
    }

    for (int index = 1; index < argc; ++index) {
        HornAudio audio;
        std::wstring error;
        if (!load_wave(std::filesystem::path(argv[index]), audio, error)) {
            std::wcerr << argv[index] << L": " << error << L"\n";
            return 1;
        }
        std::cout << "Validated " << argv[index] << " (" << audio.wave.size()
                  << " bytes).\n";
    }
    return 0;
}
