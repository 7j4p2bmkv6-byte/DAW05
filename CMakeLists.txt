cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime -> no Visual C++ redistributable needed on the target PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(LDAW VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/tracktion_engine" CACHE PATH "Tracktion Engine checkout")
if(NOT EXISTS "${TE_DIR}/modules/tracktion_engine")
    message(FATAL_ERROR "Tracktion Engine not found in ${TE_DIR}.")
endif()

set(JUCE_DIR "")
foreach(cand "${TE_DIR}/modules/juce" "${TE_DIR}/juce" "${TE_DIR}/modules/JUCE")
    if(NOT JUCE_DIR AND EXISTS "${cand}/CMakeLists.txt")
        set(JUCE_DIR "${cand}")
    endif()
endforeach()
if(NOT JUCE_DIR)
    message(FATAL_ERROR "JUCE not found inside ${TE_DIR}.")
endif()
message(STATUS "Using JUCE from: ${JUCE_DIR}")

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

set(TE_MODULE_PATHS "")
set(TE_LINK_TARGETS "")
foreach(m tracktion_core tracktion_graph tracktion_engine)
    if(EXISTS "${TE_DIR}/modules/${m}")
        list(APPEND TE_MODULE_PATHS "${TE_DIR}/modules/${m}")
        list(APPEND TE_LINK_TARGETS "tracktion::${m}")
    endif()
endforeach()
juce_add_modules(ALIAS_NAMESPACE tracktion ${TE_MODULE_PATHS})

juce_add_gui_app(LDAW
    PRODUCT_NAME "L-DAW"
    COMPANY_NAME "Landgren Audio"
    BUNDLE_ID com.landgrenaudio.ldaw
    VERSION 0.6.0)

juce_generate_juce_header(LDAW)

target_sources(LDAW PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/ArrangeView.cpp
    Source/MidiEditors.cpp
    Source/Effects.cpp
    Source/MixerView.cpp
    Source/AudioTracks.cpp
    Source/Tools.cpp
    Source/Inspector.cpp)

target_compile_definitions(LDAW PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_CAMERA=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(MSVC)
    target_compile_options(LDAW PRIVATE /bigobj /utf-8 /Zc:__cplusplus)
endif()

set(ASIO_INCLUDE_DIR "" CACHE PATH "Folder containing iasiodrv.h (ASIO SDK 'common' folder)")
if(WIN32 AND ASIO_INCLUDE_DIR AND EXISTS "${ASIO_INCLUDE_DIR}/iasiodrv.h")
    target_include_directories(LDAW PRIVATE "${ASIO_INCLUDE_DIR}")
    target_compile_definitions(LDAW PRIVATE JUCE_ASIO=1)
    message(STATUS "ASIO support: ON (${ASIO_INCLUDE_DIR})")
else()
    message(STATUS "ASIO support: OFF (WASAPI / DirectSound only)")
endif()

target_link_libraries(LDAW
    PRIVATE
        ${TE_LINK_TARGETS}
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
