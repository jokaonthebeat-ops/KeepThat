# ==============================================================================
#  KEEP THAT! - cross-platform build.
#
#  The Makefile next to this file is the macOS retail build and stays the
#  authority for what ships on Mac: it does the universal slices, the
#  Developer ID signing and the .pkg. This CMake build exists so CI can
#  produce the WINDOWS binaries, which cannot be built on the Mac at all -
#  there is no MinGW, no clang-cl and no Wine on the build machine.
#
#  It will also build on macOS and Linux, which is handy for checking that a
#  change compiles somewhere other than the hand-rolled Makefile.
#
#  NOTE: this has never been run. There is no cmake on the machine it was
#  written on, so it has not even been syntax-checked. Expect the first CI run
#  to need a fix or two; the shape is right but the details may not be.
# ==============================================================================
cmake_minimum_required(VERSION 3.22)
project(KeepThat VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/MP)              # parallel compile, MSVC is slow otherwise
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.0                  # matches the local checkout the Makefile uses
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# AU is deliberately absent: it needs Xcode, and the Mac build comes from the
# Makefile anyway. This target exists for Windows.
juce_add_plugin(KeepThat
    PRODUCT_NAME              "KEEP THAT!"
    COMPANY_NAME              "Diamond Loopz"
    COMPANY_WEBSITE           "https://diamondloopz.com"
    BUNDLE_ID                 com.diamondloopz.keepthat
    PLUGIN_MANUFACTURER_CODE  DmLz
    PLUGIN_CODE               KpTh
    FORMATS                   VST3 Standalone
    VST3_CATEGORIES           Fx Tools
    DESCRIPTION               "Always-On Idea Capture"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "KEEP THAT! needs the audio input to capture what you play."
    COPY_PLUGIN_AFTER_BUILD   FALSE)

# The sources use #include <JuceHeader.h>, so it has to be generated. The
# hand-written JucePluginDefines.h at the repo root belongs to the Makefile
# build and is NOT on the include path here - juce_add_plugin generates its
# own, and that is the one this build must see.
juce_generate_juce_header(KeepThat)

target_sources(KeepThat PRIVATE
    Source/Assets.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/capture/CaptureEngine.cpp
    Source/capture/CaptureModel.cpp
    Source/capture/ClipLoader.cpp
    Source/capture/ClipProcessor.cpp
    Source/capture/KeyDetector.cpp
    Source/capture/PhraseDetector.cpp
    Source/capture/TempoDetector.cpp
    Source/export/WavExporter.cpp
    Source/state/PresetManager.cpp)

target_compile_definitions(KeepThat PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(KeepThat
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
#  Artwork.
#
#  Assets.cpp walks UP from the running binary looking for Resources/Assets,
#  which covers every layout below:
#
#    Windows VST3   <bundle>.vst3/Contents/x86_64-win/   -> ../Resources/Assets
#    Windows app    <folder>/KEEP THAT!.exe              -> ./Resources/Assets
#
#  so the same loader works on both without a single #ifdef.
# ------------------------------------------------------------------------------
#  On macOS the standalone is an .app, so TARGET_FILE_DIR is Contents/MacOS and
#  the assets have to go one level up - putting them beside the executable
#  works only because the search walks upward, and it is the wrong place for a
#  bundle. Caught by actually building this on macOS rather than trusting the
#  Windows-shaped expression to be right everywhere.
foreach(fmt VST3 Standalone)
    if(TARGET KeepThat_${fmt})
        if(fmt STREQUAL "VST3" OR (APPLE AND fmt STREQUAL "Standalone"))
            # bundle: TARGET_FILE_DIR is <bundle>/Contents/<arch|MacOS>
            set(assetDest "$<TARGET_FILE_DIR:KeepThat_${fmt}>/../Resources/Assets")
        else()
            # bare executable (Windows/Linux standalone): Resources sits beside it
            set(assetDest "$<TARGET_FILE_DIR:KeepThat_${fmt}>/Resources/Assets")
        endif()
        add_custom_command(TARGET KeepThat_${fmt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${assetDest}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "${assetDest}"
            COMMENT "Copying artwork into the ${fmt} build")
    endif()
endforeach()
