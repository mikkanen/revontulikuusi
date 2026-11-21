# Revontulikuusi(Northern Lights Christmas Tree) (C++23 & SFML)

Reaaliaikainen, ääneen reagoiva joulukuusi- ja revontulivisualisointi modernilla C++23:lla ja SFML-grafiikkakirjastolla.

![Revontulikuusi musiikin tahdissa](assets/screenshot.png)
[![Revontulikuusi musiikin tahdissa](assets/screenshot.png)](https://www.youtube.com/watch?v=PWYqN0GBeag)

> **Katso esittelyvideo (60 FPS):** [Revontulikuusi – Wham! - Last Christmas (YouTube)](https://www.youtube.com/watch?v=PWYqN0GBeag)

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
Alternative A:
git clone [https://github.com/mikkanen/revontulikuusi.git](https://github.com/mikkanen/revontulikuusi.git)
cd revontulikuusi

cmake -B build -DSFML_DIR="C:/SFML-2.6.2/lib/cmake/SFML"
cmake --build build --config Release -j

Alternastive B:
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-sfml git
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kuusi_revontulet.exe

Ohjaus
Esc tai Q: Sulje ohjelma

