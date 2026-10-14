cmake_minimum_required(VERSION 3.22)
project(Chordtroller VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is fetched here rather than installed separately, so a fresh machine
# only needs a compiler, CMake and git. First configure downloads ~200 MB —
# point FETCHCONTENT_SOURCE_DIR_JUCE at an existing checkout to skip it.
option(CHORDTROLLER_INSTALL_PLUGIN "Copy the built VST3 into the system plugin folder (needs elevation)" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# Switch controllers — Joy-Con and Pro Controller — and their motion sensors come
# through SDL3. Xbox pads are still read directly through XInput. Static, and with
# everything SDL does that Chordtroller does not use switched off: JUCE owns the
# audio, the window and the drawing.
option(CHORDTROLLER_WITH_SDL "Read Switch controllers and their motion sensors through SDL3" ON)

if(CHORDTROLLER_WITH_SDL)
    set(SDL_SHARED        OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC        ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY  OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS         OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES      OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(SDL_AUDIO         OFF CACHE BOOL "" FORCE)
    set(SDL_RENDER        OFF CACHE BOOL "" FORCE)
    set(SDL_GPU           OFF CACHE BOOL "" FORCE)
    set(SDL_CAMERA        OFF CACHE BOOL "" FORCE)
    set(SDL_DIALOG        OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.16
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

juce_add_plugin(Chordtroller
    COMPANY_NAME            "Classical Instruments"
    # Derived from COMPANY_NAME by default, which would inherit its space and
    # produce an invalid identifier on macOS. Set explicitly.
    BUNDLE_ID               "com.classicalinstruments.chordtroller"
    PLUGIN_MANUFACTURER_CODE Clin
    PLUGIN_CODE             Chtr
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "Chordtroller"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        FALSE
    # The performance arrives on a gamepad, which the host cannot record. Notes
    # go out as MIDI so the DAW has something to keep.
    NEEDS_MIDI_OUTPUT       TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${CHORDTROLLER_INSTALL_PLUGIN})

target_sources(Chordtroller PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/input/GamepadSource.cpp
    Source/input/HandSource.cpp
    Source/input/GamepadReader.cpp
    Source/ui/InputMonitor.cpp
    Source/ui/InstrumentPanel.cpp)

target_compile_definitions(Chordtroller PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Chordtroller PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(CHORDTROLLER_WITH_SDL)
    target_sources(Chordtroller PRIVATE Source/input/SdlGamepadSource.cpp)
    target_compile_definitions(Chordtroller PRIVATE CHORDTROLLER_WITH_SDL=1)
    target_link_libraries(Chordtroller PRIVATE SDL3::SDL3-static)
endif()
