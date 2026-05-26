cmake_minimum_required(VERSION 3.22)

project(Brocot VERSION 0.1.0 LANGUAGES C CXX)

# ----------------------------------------------------------------------------
# Project-wide settings
# ----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BROCOT_BUILD_TESTS "Build Brocot unit tests" ON)

# ----------------------------------------------------------------------------
# JUCE (vendored submodule under external/JUCE)
# ----------------------------------------------------------------------------

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE submodule not found at external/JUCE. "
        "Run: git submodule update --init --recursive")
endif()

add_subdirectory(external/JUCE)

# ----------------------------------------------------------------------------
# Plugin target
# ----------------------------------------------------------------------------

juce_add_plugin(Brocot
    COMPANY_NAME            "Combust"
    COMPANY_WEBSITE         "https://codeberg.org/combust/brocot"
    BUNDLE_ID               fm.brocot.Brocot
    PLUGIN_MANUFACTURER_CODE Cbst
    PLUGIN_CODE             Brct
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "Brocot"
    DESCRIPTION             "Stern-Brocot navigated multi-modulator FM"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES         "Instrument" "Synth"
    # Sidechain input is declared-not-implemented in v1 per BROCOT-SPEC.md §2.
    # The bus is configured in PluginProcessor.cpp via BusesProperties.
)

juce_generate_juce_header(Brocot)

target_sources(Brocot PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/audio/Path.cpp
    source/audio/BrocotVoice.cpp
    source/audio/AmpEnvelope.cpp
    source/audio/DepthRatio.cpp
    source/params/ParameterTree.cpp
    source/ui/BrocotLookAndFeel.cpp
    source/ui/OperatorPanel.cpp
    source/ui/DepthSlider.cpp
    source/ui/PathNavigator.cpp
    source/ui/ModulatorPanel.cpp
    source/ui/RatioReadout.cpp
    source/ui/Oscilloscope.cpp
    source/ui/PresetBar.cpp
    source/ui/AboutDialog.cpp
    source/ui/SteerTab.cpp
    source/ui/UnderTheHoodTab.cpp
    source/ui/EnvelopePanel.cpp
    source/ui/FilterPanel.cpp
    source/presets/PresetManager.cpp
    source/presets/PresetSchema.cpp
    source/pull/PullIndex.cpp
)

target_compile_definitions(Brocot PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# Dev-mode resource path. Lets PresetManager find factory presets and tables
# without an install step when running out of build/. Production packaging
# should copy resources/ into the bundle and the .app-relative fallback in
# PresetManager.cpp takes over.
target_compile_definitions(Brocot PRIVATE
    BROCOT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(Brocot PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_plugin_client
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
)

target_link_libraries(Brocot PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_include_directories(Brocot PRIVATE source)

# ----------------------------------------------------------------------------
# Tests (Catch2 v3) — wired in here even though Slice 1 has no tests, so the
# test target exists from the start and CTest is ready.
# ----------------------------------------------------------------------------

if(BROCOT_BUILD_TESTS)
    enable_testing()

    # Try a system-installed Catch2 first (e.g., apt's catch2 package on
    # Linux). Fall back to FetchContent for platforms without a package
    # manager hit — pins to a known-good v3.5.4 release.
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        message(STATUS "System Catch2 not found; fetching v3.5.4 from GitHub")
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.5.4
        )
        FetchContent_MakeAvailable(Catch2)
        # catch_discover_tests lives in Catch2's extras/ — added to the
        # module path automatically by find_package, but not by FetchContent.
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    endif()

    add_executable(BrocotTests
        tests/PathTests.cpp
        tests/VoiceSmokeTests.cpp
        tests/MidiVoiceTests.cpp
        tests/ModGraphTests.cpp
        tests/FilterLFOTests.cpp
        tests/BesselTests.cpp
        tests/DepthRatioTests.cpp
        tests/PresetRoundtripTests.cpp
        tests/PullIndexTests.cpp
        tests/UniversalModulatorTests.cpp
        tests/OperatorTuningTests.cpp
        source/audio/DepthRatio.cpp
        source/audio/Path.cpp
        source/audio/BrocotVoice.cpp
        source/audio/AmpEnvelope.cpp
    )

    target_include_directories(BrocotTests PRIVATE source)

    target_link_libraries(BrocotTests PRIVATE
        Catch2::Catch2WithMain
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )

    # JUCE modules require these defines when compiled outside a juce_add_*
    # target. JUCE_USE_CURL/JUCE_WEB_BROWSER match what the plugin target uses
    # so juce_core doesn't pull in network/webview symbols.
    target_compile_definitions(BrocotTests PRIVATE
        JUCE_STANDALONE_APPLICATION=0
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        BROCOT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    include(Catch)
    catch_discover_tests(BrocotTests)
endif()
