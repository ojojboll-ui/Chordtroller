cmake_minimum_required(VERSION 3.22)
project(ChordtrollerShot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Renders the plugin's panel to PNG without a window. Configure the build tree
# at a short path — MSBuild's FileTracker fails (FTK1011) under deep ones:
#   cmake -S tools/panelshot -B C:\Users\oloff\ctshot

# Reuse the checkout the plugin build already fetched.
add_subdirectory("C:/Users/oloff/Documents/Claude/scriptor/build/_deps/juce-src" "${CMAKE_BINARY_DIR}/juce")

juce_add_console_app(ChordtrollerShot PRODUCT_NAME "ChordtrollerShot")

set(CT_SRC "${CMAKE_CURRENT_LIST_DIR}/../../Source")

# Without SDL: the panel does not need a pad, and XInput alone links clean.
target_sources(ChordtrollerShot PRIVATE
    main.cpp
    ${CT_SRC}/PluginProcessor.cpp
    ${CT_SRC}/PluginEditor.cpp
    ${CT_SRC}/input/GamepadSource.cpp
    ${CT_SRC}/input/GamepadReader.cpp
    ${CT_SRC}/ui/InputMonitor.cpp
    ${CT_SRC}/ui/InstrumentPanel.cpp)

target_include_directories(ChordtrollerShot PRIVATE ${CT_SRC})

target_compile_definitions(ChordtrollerShot PRIVATE
    JucePlugin_Name="Chordtroller"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=0
    JucePlugin_ProducesMidiOutput=1
    JucePlugin_IsMidiEffect=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(ChordtrollerShot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags)

# Plays the instrument through the real processor with a scripted pad in place
# of GamepadSource.cpp, and checks what reaches the host.
juce_add_console_app(ChordtrollerCheck PRODUCT_NAME "ChordtrollerCheck")

target_sources(ChordtrollerCheck PRIVATE
    check.cpp
    fake_source.cpp
    ${CT_SRC}/PluginProcessor.cpp
    ${CT_SRC}/PluginEditor.cpp
    ${CT_SRC}/input/GamepadReader.cpp
    ${CT_SRC}/ui/InputMonitor.cpp
    ${CT_SRC}/ui/InstrumentPanel.cpp)

target_include_directories(ChordtrollerCheck PRIVATE ${CT_SRC})

target_compile_definitions(ChordtrollerCheck PRIVATE
    JucePlugin_Name="Chordtroller"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=0
    JucePlugin_ProducesMidiOutput=1
    JucePlugin_IsMidiEffect=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(ChordtrollerCheck PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags)

# The hand path end to end: real UDP into the real HandSource and the processor.
juce_add_console_app(ChordtrollerHandCheck PRODUCT_NAME "ChordtrollerHandCheck")

target_sources(ChordtrollerHandCheck PRIVATE
    hand_check.cpp
    ${CT_SRC}/input/HandSource.cpp
    ${CT_SRC}/PluginProcessor.cpp
    ${CT_SRC}/PluginEditor.cpp
    ${CT_SRC}/input/GamepadReader.cpp
    ${CT_SRC}/ui/InputMonitor.cpp
    ${CT_SRC}/ui/InstrumentPanel.cpp)

target_include_directories(ChordtrollerHandCheck PRIVATE ${CT_SRC})

target_compile_definitions(ChordtrollerHandCheck PRIVATE
    JucePlugin_Name="Chordtroller"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=0
    JucePlugin_ProducesMidiOutput=1
    JucePlugin_IsMidiEffect=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(ChordtrollerHandCheck PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags)

# The same scripted pad, but it records: plays a gesture through the real
# processor and writes what the host hears to WAV files.
juce_add_console_app(ChordtrollerRender PRODUCT_NAME "ChordtrollerRender")

target_sources(ChordtrollerRender PRIVATE
    render.cpp
    fake_source.cpp
    ${CT_SRC}/PluginProcessor.cpp
    ${CT_SRC}/PluginEditor.cpp
    ${CT_SRC}/input/GamepadReader.cpp
    ${CT_SRC}/ui/InputMonitor.cpp
    ${CT_SRC}/ui/InstrumentPanel.cpp)

target_include_directories(ChordtrollerRender PRIVATE ${CT_SRC})

target_compile_definitions(ChordtrollerRender PRIVATE
    JucePlugin_Name="Chordtroller"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=0
    JucePlugin_ProducesMidiOutput=1
    JucePlugin_IsMidiEffect=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(ChordtrollerRender PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags)
