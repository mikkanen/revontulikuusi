# Revontulikuusi (C++23 & SFML)

Reaaliaikainen, ääneen reagoiva joulukuusi- ja revontulivisualisointi modernilla C++23:lla ja SFML-grafiikkakirjastolla.

## Ominaisuudet
- **Äänisynkronointi:** Kaappaa järjestelmän äänilähdön ja jakaa signaalin FFT-spektrianalyysillä kolmeen taajuuskaistaan (basso, keskiäänet, diskantti).
- **Revontulet & oskilloskooppi:** Taivaalla aaltoilevat revontulet seuraavat reaaliaikaista ääniaaltoa ja taajuusspektriä.
- **Talvimaisema:** PWM-himmennyksellä sykkivät ledit, latvatähti, lumisade, väriheijastukset lumikinoksessa, lahjapaketit, talvimökki savupiippuineen sekä tähdenlennot.
- **Alustariippumaton rakenne:**
  - **Linux:** PulseAudio / PipeWire (`@DEFAULT_MONITOR@`)
  - **macOS:** CoreAudio (`AudioQueueNewInput`)
  - **Windows 11:** WASAPI Loopback (`AUDCLNT_STREAMFLAGS_LOOPBACK`)

## Kääntäminen (CMake)

### Linux (Fedora / Ubuntu)
```bash
# Fedora:
sudo dnf install SFML-devel pulseaudio-libs-devel cmake gcc-c++

cmake -B build
cmake --build build -j
./build/kuusi_revontulet

### macOS (Homebrew)
brew install cmake sfml@2
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix sfml@2)"
cmake --build build -j
./build/kuusi_revontulet

### Windows 11 (MSYS2 UCRT64)
pacman -S mingw-w64-ucrt-x86_64-sfml mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kuusi_revontulet.exe

Ohjaus
Esc tai Q: Sulje ohjelma

