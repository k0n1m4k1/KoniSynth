// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido), antes de todo lo que guarda
// ajustes globales. No es una cabecera independiente.
#pragma once

// Configuración global del plugin (no va en presets ni en el proyecto): %APPDATA%\KoniSynth\config.settings. Es el único
// fichero de ajustes de KoniSynth junto al KoniSynth.settings del standalone: skin, AVX2, Agent, favoritos, carpeta de ROM del
// MT-32, puente MCP elegido... Cada uso abre el fichero (lee lo último que guardó otra instancia), cambia y guarda.
// Leer, cambiar y guardar es atómico entre hilos, instancias y procesos: el objeto devuelto retiene dos cerrojos mientras
// vive (de su lectura a su guardado, que PropertiesFile hace al destruirse o con saveIfNeeded). Antes, el bloqueo solo
// cubría cada lectura y cada escritura del fichero: dos instancias que guardaban a la vez perdían la clave de la primera, y
// dentro de un mismo proceso (varias instancias en un DAW) InterProcessLock no excluye a otro hilo (es reentrante por
// proceso, juce_Threads_windows.cpp:420): de ahí el CriticalSection, que se toma primero (orden fijo: sin interbloqueo).
// No guardar el objeto más allá de la función que lo usa
struct ConfigLocks
{
    static juce::CriticalSection& threadLock() { static juce::CriticalSection c; return c; }
    static juce::InterProcessLock& processLock() { static juce::InterProcessLock l ("KoniSynthConfig"); return l; }
    const juce::ScopedLock threads { threadLock() };
    juce::InterProcessLock::ScopedLockType process { processLock() };
};
// Base ConfigLocks antes que PropertiesFile: los cerrojos se toman antes de leer y se sueltan después de guardar
struct LockedConfig final : private ConfigLocks, public juce::PropertiesFile
{
    explicit LockedConfig (const juce::PropertiesFile::Options& o) : juce::PropertiesFile (o) {}
};

static std::unique_ptr<juce::PropertiesFile> openConfig()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "config";
    options.filenameSuffix = ".settings";
    options.folderName = "KoniSynth";
    options.osxLibrarySubFolder = "Application Support";
    options.processLock = &ConfigLocks::processLock();
    return std::make_unique<LockedConfig> (options);
}

// Valor de texto de config.settings. Compatibilidad: si aún no está y existe el .txt de versiones anteriores
// (%APPDATA%\KoniSynth\<legacyTxt>), se migra su contenido y el .txt se borra (solo si se guardó bien)
static juce::String configText (const juce::String& key, const char* legacyTxt)
{
    auto config = openConfig();
    const auto legacy = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("KoniSynth").getChildFile (legacyTxt);
    if (! config->containsKey (key) && legacy.existsAsFile())
    {
        config->setValue (key, legacy.loadFileAsString().trimEnd());
        if (config->saveIfNeeded())
            legacy.deleteFile();
    }
    return config->getValue (key);
}

static void setConfigText (const juce::String& key, const juce::String& value)
{
    auto config = openConfig();
    config->setValue (key, value);
    config->saveIfNeeded();
}

// Compatibilidad, al crear el procesador: pasa a config.settings los .txt de ajustes de versiones anteriores que existan y los
// borra (favoritos, carpeta de ROM del MT-32, puente elegido con Locate). El informe de la medida AVX2 (simd-selftest.txt)
// se regenera a petición: el antiguo solo se borra. skin.txt lo migra Skin::load
static void migrateLegacyConfig()
{
    configText ("favorites", "favorites.txt");
    configText ("mt32Roms", "mt32-roms.txt");
    configText ("mcpBridge", "mcp-bridge.txt");
    juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("KoniSynth").getChildFile ("simd-selftest.txt").deleteFile();
}

// Lo que aporta el usuario para los motores que emulan un aparato, todo bajo Documentos/KoniSynth/ROMs/<aparato>: MT-32
// (ROM), FM-1 (firmwares .fwsc y sus memorias), DX7 (.syx), GUS (.PAT) y SNES (.brr / .spc)
static juce::File romsFolder (const char* device)
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KoniSynth/ROMs").getChildFile (device);
}

// La carpeta elegida (si es absoluta y existe) o la de por defecto: una carpeta movida o borrada por el usuario vuelve a la
// de ROMs/ sin tocar su ajuste
static juce::File chosenOr (const juce::String& path, const char* device)
{
    const auto p = path.trim();
    return p.isNotEmpty() && juce::File::isAbsolutePath (p) && juce::File (p).isDirectory() ? juce::File (p) : romsFolder (device);
}

// Carpeta de ROM del MT-32 / CM-32L: la elegida por el usuario (clave mt32Roms) o ROMs/MT-32. KONI_MT32_ROMS (pruebas y
// KoniBench) la sustituye sin tocar el ajuste del usuario
static juce::File mt32RomFolder()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("KONI_MT32_ROMS", {}); env.isNotEmpty())
        return juce::File (env);
    return chosenOr (configText ("mt32Roms", "mt32-roms.txt"), "MT-32");
}

static void setMt32RomFolder (const juce::File& folder) { setConfigText ("mt32Roms", folder.getFullPathName()); }

// Carpeta de patches del Gravis UltraSound (.PAT con timidity.cfg o DEFAULT.CFG): la elegida (clave gusPatches) o
// ROMs/GUS. KONI_GUS_PATCHES (pruebas y KoniBench) la sustituye sin tocar el ajuste del usuario
static juce::File gusPatchFolder()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("KONI_GUS_PATCHES", {}); env.isNotEmpty())
        return juce::File (env);
    auto config = openConfig();
    return chosenOr (config != nullptr ? config->getValue ("gusPatches") : juce::String(), "GUS");
}

static void setGusPatchFolder (const juce::File& folder) { setConfigText ("gusPatches", folder.getFullPathName()); }

// Carpeta de firmwares del M-VAVE FM-1: una subcarpeta por firmware con sus versiones (.fwsc), p. ej. M-VAVE/FM-1_015.fwsc o
// GHOULBOX/ghoulbox-2.4.fwsc. La elegida (clave fm1Firmware) o ROMs/FM-1. KONI_FM1_FIRMWARE (pruebas y KoniBench) la sustituye
// sin tocar el ajuste del usuario. KoniSynth no incluye ningún firmware: los aporta el usuario
static juce::File fm1FirmwareFolder()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("KONI_FM1_FIRMWARE", {}); env.isNotEmpty())
        return juce::File (env);
    auto config = openConfig();
    return chosenOr (config != nullptr ? config->getValue ("fm1Firmware") : juce::String(), "FM-1");
}

// Donde empieza el selector de bancos DX7 (.syx): ROMs/DX7 (el .syx elegido va en el preset; no es un ajuste)
static juce::File dx7BankFolder() { return romsFolder ("DX7"); }

static void setFm1FirmwareFolder (const juce::File& folder) { setConfigText ("fm1Firmware", folder.getFullPathName()); }

// El último firmware del FM-1 que arrancó (clave fm1LastFirmware): sin firmware en el preset o el proyecto (los de otros
// motores no lo traen), arranca este y no el primero de la carpeta
static juce::File fm1LastFirmware()
{
    auto config = openConfig();
    const auto path = config != nullptr ? config->getValue ("fm1LastFirmware") : juce::String();
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}
static void setFm1LastFirmware (const juce::File& file) { setConfigText ("fm1LastFirmware", file.getFullPathName()); }

// Latencia del FM-1 (ms): la cola de audio por delante de la que tira el host (Engines/Fm1.h). Más larga aguanta los picos
// del emulador (cambios rápidos de tono, muchas voces) a costa de retraso. Clave fm1LatencyMs; 30 ms por defecto
static constexpr int kFm1Latencies[] = { 30, 60, 120, 250, 500 };
static int fm1LatencyMs()
{
    auto config = openConfig();
    const auto v = config != nullptr ? config->getIntValue ("fm1LatencyMs", 30) : 30;
    for (const auto l : kFm1Latencies)
        if (l == v) return v;
    return 30;
}
static void setFm1LatencyMs (int ms) { setConfigText ("fm1LatencyMs", juce::String (ms)); }
