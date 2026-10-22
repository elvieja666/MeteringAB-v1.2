# ---------------------------------------------------------------------------
#  Metering A/B - plugin de matcheo de loudness Send/Receive
#  Este archivo le dice a CMake cómo armar el plugin. Descarga JUCE solo,
#  así que no hace falta instalar JUCE a mano.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.22)

# En Mac: compilar para Apple Silicon e Intel, compatible desde macOS 11.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Versión mínima de macOS")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Arquitecturas")

project(MeteringAB VERSION 1.0.5 LANGUAGES C CXX)   # subir la versión hace que Logic vuelva a leer el nombre

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Descarga JUCE la primera vez que configurás el proyecto.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MeteringAB
    COMPANY_NAME             "DIY"
    BUNDLE_ID                "com.0600musica.meteringab"
    PLUGIN_MANUFACTURER_CODE Ms06        # 4 letras, al menos una mayúscula
    PLUGIN_CODE              Mtab        # 4 letras, única para este plugin
    PRODUCT_NAME             "Metering AB"   # nombre del archivo (sin /)
    PLUGIN_NAME              "Metering A/B"  # nombre que muestra Logic
    FORMATS                  AU VST3     # AU es el que usa Logic
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE)       # lo copia solo a la carpeta de plugins

# Letra Inter (licencia OFL, ver Resources/Fonts/OFL.txt), incluida en el plugin
# para que se vea igual en cualquier Mac.
juce_add_binary_data(MeteringABFonts SOURCES
    Resources/Fonts/Inter-Light.otf
    Resources/Fonts/Inter-Regular.otf
    Resources/Fonts/Inter-Medium.otf
    Resources/Fonts/Inter-SemiBold.otf)
set_target_properties(MeteringABFonts PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(MeteringAB PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(MeteringAB PRIVATE Source)

target_compile_definitions(MeteringAB PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MeteringAB
    PRIVATE
        MeteringABFonts
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
#  Tests automáticos (opcionales): cmake -B build -DMETERINGAB_TESTS=ON
# ---------------------------------------------------------------------------
option(METERINGAB_TESTS "Compilar los tests" OFF)
if(METERINGAB_TESTS)
    juce_add_console_app(MeteringABTests PRODUCT_NAME "MeteringABTests")
    target_sources(MeteringABTests PRIVATE
        Tests/MeteringABTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_include_directories(MeteringABTests PRIVATE Source)
    target_compile_definitions(MeteringABTests PRIVATE
        JucePlugin_Name="Metering A/B" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MeteringABTests PRIVATE
        MeteringABFonts juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
