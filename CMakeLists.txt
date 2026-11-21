cmake_minimum_required(VERSION 3.25)
project(Revontulikuusi VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Etsi SFML (Linux: /usr, macOS: Homebrew, Windows: MSYS2 / vcpkg)
find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)

set(SOURCES
    src/main.cpp
)

if(WIN32)
    # Windows 11: WASAPI Loopback
    list(APPEND SOURCES src/AudioData/audio_capture_windows.cpp)
    add_executable(kuusi_revontulet ${SOURCES})
    target_link_libraries(kuusi_revontulet PRIVATE
        sfml-graphics
        sfml-window
        sfml-system
        sfml-main
        ole32
        avrt
    )
    # Estetään ylimääräisen konsoli-ikkunan aukeaminen Release-käännöksessä
    if(MSVC)
        set_target_properties(kuusi_revontulet PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
    elseif(MINGW)
        target_link_options(kuusi_revontulet PRIVATE $<$<CONFIG:Release>:-mwindows>)
    endif()

elseif(APPLE)
    # macOS: CoreAudio + AudioToolbox
    list(APPEND SOURCES src/AudioData/audio_capture_macos.cpp)
    add_executable(kuusi_revontulet ${SOURCES})
    target_link_libraries(kuusi_revontulet PRIVATE
        sfml-graphics
        sfml-window
        sfml-system
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )

elseif(UNIX)
    # Linux: PulseAudio / PipeWire-pulse
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse-simple libpulse)

    list(APPEND SOURCES src/AudioData/audio_capture_linux.cpp)
    add_executable(kuusi_revontulet ${SOURCES})
    target_link_libraries(kuusi_revontulet PRIVATE
        sfml-graphics
        sfml-window
        sfml-system
        PkgConfig::PULSE
    )
endif()

target_include_directories(kuusi_revontulet PRIVATE src src/AudioData)

if(MSVC)
    target_compile_options(kuusi_revontulet PRIVATE /W4 /O2 /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kuusi_revontulet PRIVATE -Wall -Wextra -O2)
endif()