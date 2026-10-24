cmake_minimum_required(VERSION 3.22)
project(KoniSynth VERSION 1.0.0) # numérica (VST3/CLAP); la visible es KONI_VERSION_LABEL

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime de C++ estático (/MT): los binarios no dependen de MSVCP140/VCRUNTIME140 (sin Redistributable en el
# equipo de destino). Antes de FetchContent para que JUCE use el mismo runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# MSVC: /Gy (una sección COMDAT por función) en todo, también en JUCE y dependencias: sin ella el enlazador descarta el
# LTCG incremental de KONISYNTH_FAST_LINK ("Not all modules are compiled with -Gy"). Antes de FetchContent para que les alcance
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Gy>)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(KoniSynth
    COMPANY_NAME             "KoniSynth"
    PRODUCT_NAME             "KoniSynth"
    PLUGIN_MANUFACTURER_CODE Koni
    PLUGIN_CODE              Ksyn
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    ICON_BIG                 Resources/KoniSynth-512.png # icono del ejecutable (JUCE genera el .ico; original: KoniSynth.png)
    FORMATS                  VST3 Standalone)

target_sources(KoniSynth PRIVATE Source/KoniSynth.cpp Source/SimdAvx2.cpp
    Source/ThirdParty/NukedOPLL/opll.c # Nuked-OPLL, GPL-2.0+ (YM2413 / VRC7)
    Source/ThirdParty/MTS-ESP/libMTSClient.cpp) # cliente MTS-ESP (ODDSound), 0BSD: carga LIBMTS.dll solo si está instalada
# Código de terceros sin modificar: sin avisos (los suyos no son de KoniSynth)
set_source_files_properties(Source/ThirdParty/NukedOPLL/opll.c PROPERTIES COMPILE_OPTIONS "$<$<C_COMPILER_ID:MSVC>:/w>")
set_source_files_properties(Source/ThirdParty/MTS-ESP/libMTSClient.cpp PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/w>")
# Munt mt32emu 2.8.3 (Roland MT-32 / CM-32L, LGPL-2.1+): biblioteca estática sin modificar (config.h generado a mano). Las ROM
# de Roland no se incluyen: las aporta el usuario
file(GLOB MT32EMU_SOURCES Source/ThirdParty/mt32emu/src/*.cpp Source/ThirdParty/mt32emu/src/sha1/*.cpp
    Source/ThirdParty/mt32emu/src/srchelper/*.cpp Source/ThirdParty/mt32emu/src/srchelper/srctools/src/*.cpp)
add_library(mt32emu STATIC ${MT32EMU_SOURCES})
target_compile_definitions(mt32emu PUBLIC MT32EMU_WITH_INTERNAL_RESAMPLER)
target_include_directories(mt32emu PUBLIC Source/ThirdParty/mt32emu/src)
target_compile_options(mt32emu PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/w>")
set_target_properties(mt32emu PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(KoniSynth PRIVATE mt32emu)
# Motores, DSP, efectos, MIDI, parámetros, presets e interfaz: cabeceras incluidas desde KoniSynth.cpp (listadas para el IDE)
file(GLOB KONI_HEADERS CONFIGURE_DEPENDS Source/Engines/*.h Source/Dsp/*.h Source/Effects/*.h Source/Midi/*.h Source/Params/*.h Source/Presets/*.h Source/UI/*.h Source/Midi/Controllers/*.h Source/Import/*.h)
target_sources(KoniSynth PRIVATE ${KONI_HEADERS})

# CLAP (experimental): JUCE 9.0.4 no lo trae de serie (tampoco la rama develop, 2026-10-20); clap-juce-extensions
# (MIT, free-audio) lo añade como formato extra. Fijado a un commit (main de 2026-10-06) para builds reproducibles.
# clap-validator 0.4.1: 30 de 44 pruebas pasan; fallan las 3 de state-reproducibility (causa no verificada).
option(KONISYNTH_CLAP "Build the CLAP format with clap-juce-extensions" ON)
if(KONISYNTH_CLAP)
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(clap_juce_extensions)
    clap_juce_extensions_plugin(TARGET KoniSynth
        CLAP_ID "com.konisynth.konisynth"
        CLAP_FEATURES instrument synthesizer stereo)
endif()

# Solo desarrollo (MSVC): LTCG incremental. JUCE enlaza con /LTCG completo y cada formato repite toda la generación de
# código (≈ 57 s por formato). Con /LTCG:INCREMENTAL el enlazador guarda .iobj/.ipdb y solo regenera las funciones que
# cambian. Medido (cambio en Displays.h, VST3 + Standalone, /m:1 CL_MPCount=6): 130 s → 34 s, renders de regresión
# idénticos, pero +18-23 % de CPU en los escenarios pesados de KoniBench. Apagado por defecto: las
# releases y las medidas de CPU se hacen con el LTCG completo
option(KONISYNTH_FAST_LINK "Incremental LTCG for faster development rebuilds (slower binaries; never for releases)" OFF)
if(MSVC AND KONISYNTH_FAST_LINK)
    foreach(t KoniSynth_VST3 KoniSynth_Standalone KoniSynth_CLAP)
        if(TARGET ${t})
            target_link_options(${t} PRIVATE $<$<CONFIG:Release>:/LTCG:INCREMENTAL>)
        endif()
    endforeach()
endif()

# Ruta AVX2: solo este fichero se compila con AVX2 y sin LTCG (/GL-), para que ningún código AVX2 se inline ni se
# comparta con el resto; el plugin elige la ruta en tiempo de ejecución (CPU + autoprueba + casilla en Settings).
if(MSVC)
    set_source_files_properties(Source/SimdAvx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2;/GL-")
elseif(APPLE)
    # macOS universal (arm64 + x86_64): AVX2 solo en la porción x86_64; en arm64 el fichero compila sus funciones vacías
    set_source_files_properties(Source/SimdAvx2.cpp PROPERTIES COMPILE_OPTIONS "-Xarch_x86_64;-mavx2;-Xarch_x86_64;-mfma")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    set_source_files_properties(Source/SimdAvx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
endif()

target_compile_definitions(KoniSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    KONI_VERSION_LABEL="1.0.0-alpha") # versión visible (título, About, zips)
# Solo para desarrollo: el diálogo Agent (MCP) busca el puente también en Agent/KoniSynth-mcp de este repositorio. Apagado
# por defecto: una build de release no incrusta rutas del equipo que la compila
option(KONISYNTH_EMBED_REPO_PATH "Let the Agent dialog find the MCP bridge in this source tree (development builds)" OFF)
if(KONISYNTH_EMBED_REPO_PATH)
    target_compile_definitions(KoniSynth PUBLIC KONI_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Recursos incrustados (diálogo About): avatar de https://steamcommunity.com/id/k0n1m4k1 (avatarFull, 184 × 184 JPEG)
juce_add_binary_data(KoniSynthData HEADER_NAME BinaryData.h NAMESPACE BinaryData
    SOURCES Resources/about-avatar.jpg)

target_link_libraries(KoniSynth
    PRIVATE KoniSynthData
            juce::juce_audio_utils
            juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# Banco de medida: carga el VST3 compilado y mide processBlock (ver Bench/KoniBench.cpp)
juce_add_console_app(KoniBench PRODUCT_NAME "KoniBench")
target_sources(KoniBench PRIVATE Bench/KoniBench.cpp Source/ThirdParty/MTS-ESP/libMTSClient.cpp)
target_compile_definitions(KoniBench PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(KoniBench
    PRIVATE juce::juce_audio_processors juce::juce_audio_formats
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
