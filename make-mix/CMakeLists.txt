cmake_minimum_required(VERSION 3.22)
project(MakeMix VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE：本地可以用 -DJUCE_DIR=/path/to/JUCE 指定，否则自动下载（固定版本，结果可复现）
if(DEFINED JUCE_DIR)
  add_subdirectory(${JUCE_DIR} JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.10 GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

option(MAKEMIX_TOOLS "Build the offline render tool (vox_render)" ON)

juce_add_plugin(MakeMix
  COMPANY_NAME "MAKE STUDIO"
  COMPANY_WEBSITE "https://www.makestudio.cn"
  PLUGIN_MANUFACTURER_CODE Mkst
  PLUGIN_CODE Mmix
  FORMATS VST3
  PRODUCT_NAME "MAKE MIX"
  VERSION ${PROJECT_VERSION}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  VST3_CATEGORIES Fx Dynamics EQ Mastering
  COPY_PLUGIN_AFTER_BUILD FALSE)

set(VOX_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(VOX_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_sources(MakeMix PRIVATE ${VOX_SOURCES})
target_compile_definitions(MakeMix PUBLIC ${VOX_DEFS})
target_link_libraries(MakeMix PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(MAKEMIX_TOOLS)
  # 离线渲染：vox_render in.wav out.wav [预设序号] [BPM] [参数=值 ...]，用来听效果、测 CPU
  juce_add_console_app(VoxRender PRODUCT_NAME "vox_render")
  target_sources(VoxRender PRIVATE tools/render.cpp ${VOX_SOURCES})
  target_compile_definitions(VoxRender PRIVATE ${VOX_DEFS} JUCE_USE_MP3AUDIOFORMAT=1)
  target_link_libraries(VoxRender PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
