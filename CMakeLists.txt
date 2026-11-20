cmake_minimum_required(VERSION 3.22)
# Versionsnummer steht in der Datei VERSION (wird bei jeder Aenderung hochgezaehlt)
file(STRINGS VERSION MOVES_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)
project(Moves VERSION ${MOVES_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Laufzeitbibliothek fest einbinden: das Plugin braucht dann kein VC++-Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(AUDIO_DSP_ONLY "Build portable audio regression tests without JUCE" OFF)
include(CTest)
if(BUILD_TESTING)
    add_executable(MovesDSPTests Tests/DSPTests.cpp)
    target_include_directories(MovesDSPTests PRIVATE Source)
    add_test(NAME DSP COMMAND MovesDSPTests)
endif()
if(AUDIO_DSP_ONLY)
    return()
endif()

set(MOVES_JUCE_PATH "" CACHE PATH "Lokaler JUCE-Ordner (leer = automatisch laden)")
set(MOVES_FORMATS "VST3" CACHE STRING "Plugin-Formate")

if(MOVES_JUCE_PATH)
    add_subdirectory(${MOVES_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Eingebaute MOVES-Samplebibliothek (Koerper-/Kleidungsbewegungen, Atmer/Schniefer).
# Nur eingebaut, wenn die Ordner lokal vorhanden sind - robust gegen ein Gerueust ohne Audiomaterial.
set(MOVES_ASSET_SOURCES Assets/logo.png)
set(MOVES_SAMPLE_DEFS "MOVES_HAS_SAMPLES=0")
file(GLOB MOVES_MOVEMENT_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Assets/samples/movements/*.wav)
file(GLOB MOVES_BREATH_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Assets/samples/breaths_sniffs/*.wav)
if(MOVES_MOVEMENT_FILES OR MOVES_BREATH_FILES)
    list(APPEND MOVES_ASSET_SOURCES ${MOVES_MOVEMENT_FILES} ${MOVES_BREATH_FILES})
    set(MOVES_SAMPLE_DEFS "MOVES_HAS_SAMPLES=1")
endif()
juce_add_binary_data(MovesAssets SOURCES ${MOVES_ASSET_SOURCES})

# Selbst klingender Effekt ohne Eingangssignal: kein Eingangsbus, nur Ausgang.
# VST3_CATEGORIES "Fx Generator" passt zu einem Effekt, der sich selbst erzeugt (wie TUMULT),
# nicht IS_SYNTH, weil kein Instrument im klassischen Sinn (keine Midi-Noten).
juce_add_plugin(Moves
    COMPANY_NAME "Stromschlag Audio"
    BUNDLE_ID "com.stromschlagaudio.moves"
    PLUGIN_MANUFACTURER_CODE Strm
    PLUGIN_CODE Move
    FORMATS ${MOVES_FORMATS}
    PRODUCT_NAME "MOVES"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Generator
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Moves PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Moves PUBLIC
    PLUGIN_VERSION_TEXT="${MOVES_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    ${MOVES_SAMPLE_DEFS})

if(MSVC)
    target_compile_options(Moves PRIVATE /utf-8)
endif()

target_link_libraries(Moves
    PRIVATE
        MovesAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(BUILD_TESTING)
    juce_add_console_app(MovesProcessorTests PRODUCT_NAME "MOVES processor tests")
    target_sources(MovesProcessorTests PRIVATE Tests/ProcessorTests.cpp)
    target_include_directories(MovesProcessorTests PRIVATE Source $<TARGET_PROPERTY:Moves,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MovesProcessorTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MovesProcessorTests PRIVATE Moves juce::juce_audio_utils)
    add_test(NAME Processor COMMAND MovesProcessorTests)
endif()

# Real editor snapshots, excluded from normal plugin/release builds.
juce_add_console_app(MovesScreenshot PRODUCT_NAME "MOVES screenshot")
target_sources(MovesScreenshot PRIVATE tools/screenshot.cpp)
target_include_directories(MovesScreenshot PRIVATE Source $<TARGET_PROPERTY:Moves,INCLUDE_DIRECTORIES>)
target_compile_definitions(MovesScreenshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 PLUGIN_VERSION_TEXT="${MOVES_VERSION}")
target_link_libraries(MovesScreenshot PRIVATE Moves)
set_target_properties(MovesScreenshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
