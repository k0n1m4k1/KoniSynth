// Se incluye desde KoniSynth.cpp, dentro de la misma unidad de compilación (JUCE ya incluido), antes de todo lo que guarda
// ajustes globales. No es una cabecera independiente.
#pragma once

// Configuración global del plugin (no va en presets ni en el proyecto): %APPDATA%\KoniSynth\config.settings. Es el único
// fichero de ajustes de KoniSynth junto al KoniSynth.settings del standalone: skin, AVX2, Agent, favoritos, carpeta de ROM del
// MT-32, puente MCP elegido... Cada uso abre el fichero (lee lo último que guardó otra instancia), cambia y guarda; el
// bloqueo entre procesos serializa las lecturas y escrituras del fichero entre hilos e instancias del plugin
static std::unique_ptr<juce::PropertiesFile> openConfig()
{
    static juce::InterProcessLock lock ("KoniSynthConfig");
    juce::PropertiesFile::Options options;
    options.applicationName = "config";
    options.filenameSuffix = ".settings";
    options.folderName = "KoniSynth";
    options.osxLibrarySubFolder = "Application Support";
    options.processLock = &lock;
    return std::make_unique<juce::PropertiesFile> (options);
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

// Carpeta de ROM del MT-32 / CM-32L: la elegida por el usuario (clave mt32Roms) o Documentos/KoniSynth/ROMs/MT-32.
// KONI_MT32_ROMS (pruebas y KoniBench) la sustituye sin tocar el ajuste del usuario
static juce::File mt32RomFolder()
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("KONI_MT32_ROMS", {}); env.isNotEmpty())
        return juce::File (env);
    const auto path = configText ("mt32Roms", "mt32-roms.txt").trim();
    return path.isNotEmpty() && juce::File::isAbsolutePath (path)
               ? juce::File (path)
               : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("KoniSynth/ROMs/MT-32");
}

static void setMt32RomFolder (const juce::File& folder) { setConfigText ("mt32Roms", folder.getFullPathName()); }
