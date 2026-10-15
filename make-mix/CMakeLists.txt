cmake_minimum_required(VERSION 3.22)
project(MakeMix VERSION 1.3.0 LANGUAGES C CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Windows：静态运行库，不依赖 VC++ 再发行包（命令行可以覆盖）
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MAKEMIX_TOOLS "Build the mm_mixrender test harness" ON)
option(MAKEMIX_TSAN "ThreadSanitizer for mm_mixrender; never the plugin" OFF)
option(MAKEMIX_ASAN "AddressSanitizer + UBSan for the console targets; never the plugin" OFF)
option(MAKEMIX_LTO "Link-time optimisation for the plugin (Release)" ON)
if(MAKEMIX_TSAN AND MAKEMIX_ASAN)
  message(FATAL_ERROR "MAKEMIX_TSAN and MAKEMIX_ASAN cannot be combined; use separate build directories")
endif()

# JUCE：本地用 -DJUCE_DIR=/path/to/JUCE，否则自动下载固定版本（结果可复现）
if(JUCE_DIR)
  add_subdirectory(${JUCE_DIR} JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.10 GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------------------------
# 源文件
set(MM_HEADERS
  Source/Shared.h Source/Params.h Source/Styles.h Source/Dsp.h Source/Chains.h Source/Analysis.h Source/AutoMix.h
  Source/Hub.h Source/PluginProcessor.h Source/PluginEditor.h)
set(MM_SOURCES
  Source/Chains.cpp Source/Analysis.cpp Source/AutoMix.cpp Source/Hub.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
set(MM_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
# 源码里有中文注释和中日韩界面字符串：MSVC 必须按 UTF-8 读
set(MM_OPTS $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# ---------------------------------------------------------------------------------------------
# 插件：MakeMix_VST3 → build/MakeMix_artefacts/<Config>/VST3/MAKE MIX.vst3
juce_add_plugin(MakeMix
  COMPANY_NAME "MAKE STUDIO"
  COMPANY_WEBSITE "https://www.makestudio.cn"
  BUNDLE_ID cn.makestudio.makemix
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
target_sources(MakeMix PRIVATE ${MM_SOURCES} ${MM_HEADERS})
target_compile_definitions(MakeMix PUBLIC ${MM_DEFS})
target_compile_options(MakeMix PUBLIC ${MM_OPTS})
target_link_libraries(MakeMix PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(MAKEMIX_LTO)
  target_link_libraries(MakeMix PUBLIC juce::juce_recommended_lto_flags)
endif()

# ---------------------------------------------------------------------------------------------
# 命令行工具和测试共用一个静态库 mm_core（我们的源文件 + JUCE 模块只编一次）。
# JUCE 模块以 PRIVATE 链接进 mm_core，再把它的头文件路径和宏用 INTERFACE 转给使用者，
# 这样使用者不会再编一遍 JUCE 模块的源文件（否则会重复定义）。
if(MAKEMIX_TOOLS)
  add_library(mm_core STATIC ${MM_SOURCES} ${MM_HEADERS})
  target_compile_definitions(mm_core PUBLIC ${MM_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_USE_MP3AUDIOFORMAT=1)
  target_compile_options(mm_core PUBLIC ${MM_OPTS})
  target_link_libraries(mm_core
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
    PUBLIC juce::juce_recommended_warning_flags)
  if(NOT (MAKEMIX_TSAN OR MAKEMIX_ASAN))
    # 它会在目标自己的选项之后追加 -O3；消毒器构建不用它，让下面的 -O1 生效
    target_link_libraries(mm_core PUBLIC juce::juce_recommended_config_flags)
  endif()
  target_include_directories(mm_core INTERFACE $<TARGET_PROPERTY:mm_core,INCLUDE_DIRECTORIES>)
  target_compile_definitions(mm_core INTERFACE $<TARGET_PROPERTY:mm_core,COMPILE_DEFINITIONS>)
  set_target_properties(mm_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

  if(MAKEMIX_TSAN OR MAKEMIX_ASAN)
    if(MSVC)
      if(MAKEMIX_TSAN)
        message(FATAL_ERROR "ThreadSanitizer is not available with MSVC")
      endif()
      target_compile_options(mm_core PUBLIC /fsanitize=address)
    else()
      if(MAKEMIX_TSAN)
        set(MM_SAN -fsanitize=thread)
      else()
        set(MM_SAN -fsanitize=address,undefined -fno-omit-frame-pointer)
      endif()
      target_compile_options(mm_core PUBLIC ${MM_SAN} -g -O1)
      target_link_options(mm_core PUBLIC ${MM_SAN})
    endif()
  endif()

  # 多实例测试工具：模拟 FL（人声 + 伴奏 + 总线三个实例共用一个 hub），学习 → 解算 → 渲染 → 指标
  add_executable(mm_mixrender tools/mixrender.cpp)
  target_link_libraries(mm_mixrender PRIVATE mm_core)
endif()
