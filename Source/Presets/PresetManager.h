// Extraído de KoniSynth.cpp (2026-10-21): se incluye desde allí, en el mismo punto, dentro de la misma unidad de
// compilación; usa las declaraciones previas de KoniSynth.cpp (JUCE, Params, utilidades). No es una cabecera independiente.
#pragma once

// Presets: fábrica (solo lectura) + usuario (un .xml por preset en %APPDATA%\KoniSynth\Presets).
// El preset actual se guarda como propiedad de apvts.state, así viaja con el estado del plugin.
// Versión del formato (atributo formatVersion; sin él = 1): al cargar, migrateValues lleva los valores de un formato
// anterior al actual; un preset de un formato posterior se carga con lo que se entiende y se avisa (lastLoadNote).
// Favoritos: un fichero de texto (una clave por línea) junto a la carpeta de presets.
// Todo salvo invalidateSnapshot() se llama solo desde el hilo de mensajes.
class PresetManager
{
public:
    struct Preset
    {
        juce::String name;
        bool factory = false;
        juce::File file;
        juce::String category = "Init";
        int engine = 0; // índice del parámetro "engine" que deja el preset (navegador: filtro por motor)
        juce::String playHint; // cómo tocarlo (FactoryPreset::playHint; en los de usuario, el que se guardó con él)
        int playLow = -1, playHigh = -1; // rango recomendado (notas MIDI; -1 = sin rango)
        // Categoría escrita en el fichero si no es una de presetCategories() (preset de una versión posterior): la lista lo
        // muestra en Init, pero Save la conserva en vez de reescribirla como Init. Vacía si es conocida
        juce::String unknownCategory {};
        // Fuentes del motor Synth que suenan (nivel > 0): un sample del usuario (fuente Sample, Osc 2 u Osc 3 en modo Sample, con
        // fichero) y la fuente Math Models (cualquiera de sus modelos, Modal incluido). Las listas lo marcan tras el nombre: "(s)"
        // y "(π)". Los de fábrica no llevan samples: (s) solo aparece en presets de usuario
        bool sampleSource = false, mathSource = false;

        juce::String displayName() const
        {
            return name + (sampleSource ? " (s)" : "") + (mathSource ? juce::String (juce::CharPointer_UTF8 (" (\xcf\x80)")) : juce::String()); // π = U+03C0
        }
    };

    // get (id, valor por defecto): valor real de un parámetro del preset. samplePath, osc2SamplePath y osc3SamplePath: sus
    // ficheros (vacío = ninguno)
    template <typename Get>
    static void markSources (Preset& p, Get&& get, const juce::String& samplePath, const juce::String& osc2SamplePath,
                             const juce::String& osc3SamplePath)
    {
        if (juce::roundToInt (get ("engine", 0.0f)) != 0)
            return;
        // Sample de Osc 2 / Osc 3: su nivel (formato 8) o, en ficheros anteriores sin migrar, la forma Sample con el nivel del oscilador
        p.sampleSource = (get ("sampleLevel", 0.0f) > 0.0f && samplePath.isNotEmpty())
                      || ((get ("osc2SampleLevel", 0.0f) > 0.0f
                           || (juce::roundToInt (get ("osc2Wave", 2.0f)) == legacyWaveSample && get ("osc2Level", 0.0f) > 0.0f))
                          && osc2SamplePath.isNotEmpty())
                      || ((get ("osc3SampleLevel", 0.0f) > 0.0f
                           || (juce::roundToInt (get ("osc3Wave", 3.0f)) == legacyWaveSample && get ("subLevel", 0.0f) > 0.0f))
                          && osc3SamplePath.isNotEmpty());
        p.mathSource = get ("stringLevel", 0.0f) > 0.0f;
    }
    // osc2Wave / osc3Wave hasta el formato 7: Wavetable y Sample eran las opciones 10 y 11 (splitOscFamilies)
    static constexpr int legacyWaveTable = 10, legacyWaveSample = 11;

    // Cómo tocar el preset actual: propiedades del estado (no son parámetros). Las fija la carga de un preset, se pueden
    // editar desde el teclado en pantalla (cuentan como cambio sin guardar, "*") y las escribe Save / Save As
    static inline const juce::Identifier playHintProp { "playHint" }, playLowProp { "playLow" }, playHighProp { "playHigh" };

    // 1: hasta la 0.6 (sin atributo). 2: formatVersion y pluginVersion; thereminOn ya migrado a engine. 3: Osc 2 en modo Sample, con su
    // fichero (atributo osc2SamplePath). 4: tabla propia de Osc 2 en modo Wavetable (osc2WtTable, fichero en osc2WtPath) y
    // osc2Wave con Wavetable y Sample al final de la lista. 5: KSL de los operadores OPL en dB/oct (antes, en OPL2/OPL3, el valor
    // de registro: 1 = 3 dB/oct, 2 = 1,5 dB/oct). 6: sample propio de cada motor de consola (dpcmSamplePath, mdSamplePath,
    // snesSamplePath, n64SamplePath y sus parámetros de región); antes usaban el de la fuente Sample del Synth.
    // Sin cambio de formato (1.0.1): Osc 3 con Wavetable y Sample al final de osc3Wave (los índices anteriores no cambian),
    // osc3Sync / osc3WtPosition / osc3WtTable / osc3Sample* (ausentes: su valor por defecto) y los ficheros osc3SamplePath y
    // osc3WtPath (ausentes: sin fichero); complexModSource con Noise al final. 7 (1.0.1): Osc 3 con Oct -2..+2 (osc3Octave) en vez
    // de subOctave ("-1 oct" / "-2 oct") y Semi ±12 en vez de ±24 (osc3FromSub). 8 (1.0.1): Wavetable y Sample de Osc 2 / Osc 3
    // dejan de ser formas de onda y pasan a fuentes con su nivel (splitOscFamilies)
    static constexpr int formatVersion = 8;

    explicit PresetManager (APVTS& s) : apvts (s)
    {
        loadFavorites();
        refresh();
        if (! apvts.state.hasProperty (nameProp))
            setCurrent (presets.front()); // "Init" = valores por defecto
    }

    static juce::File userDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("KoniSynth").getChildFile ("Presets");
    }

    void refresh()
    {
        presets.clear();
        for (const auto& f : factoryPresets())
        {
            int engine = 0;
            for (const auto& v : f.values)
                if (v.id == "engine") engine = juce::roundToInt (v.value);
            presets.push_back ({ f.name, true, {}, f.category, engine, f.playHint, f.playLow, f.playHigh });
            markSources (presets.back(), [&f] (const char* id, float def)
                         {
                             for (const auto& v : f.values)
                                 if (v.id == id) return v.value;
                             return def;
                         }, f.samplePath, f.osc2SamplePath, juce::String()); // los de fábrica no llevan sample de Osc 3
        }

        auto files = userDirectory().findChildFiles (juce::File::findFiles, false, "*.xml");
        files.sort();
        for (const auto& f : files)
        {
            // Categoría del preset de usuario: atributo presetCategory (sin él, presets anteriores -> "Init")
            juce::String category ("Init"), hint;
            int engine = 0, low = -1, high = -1;
            const auto xml = juce::XmlDocument::parse (f);
            if (xml != nullptr && xml->hasTagName (xmlTag))
            {
                category = xml->getStringAttribute (categoryAttribute, "Init");
                hint = xml->getStringAttribute (playHintAttribute);
                low = xml->getIntAttribute (playLowAttribute, -1);
                high = xml->getIntAttribute (playHighAttribute, -1);
                engine = juce::roundToInt (xml->hasAttribute ("engine") ? xml->getDoubleAttribute ("engine")
                                                                        : (xml->getDoubleAttribute ("thereminOn") > 0.5 ? 1.0 : 0.0));
            }
            const auto known = presetCategories().contains (category);
            presets.push_back ({ f.getFileNameWithoutExtension(), false, f, known ? category : juce::String ("Init"), engine, hint, low, high,
                                 known ? juce::String() : category });
            if (xml != nullptr && xml->hasTagName (xmlTag))
                markSources (presets.back(), [&xml] (const char* id, float def) { return (float) xml->getDoubleAttribute (id, def); },
                             xml->getStringAttribute (sampleAttribute), xml->getStringAttribute (osc2SampleAttribute),
                             xml->getStringAttribute (osc3SampleAttribute));
        }
    }

    const std::vector<Preset>& getPresets() const { return presets; }

    // Favoritos (navegador). Clave: "F:" fábrica / "U:" usuario + nombre. Tienen un orden propio, el del usuario (las teclas
    // 1-9 cargan los nueve primeros); uno nuevo va al final. Las claves de presets que ya no existen se conservan en el
    // fichero (un preset de usuario renombrado fuera vuelve a su sitio) pero no cuentan para la posición
    bool isFavorite (size_t index) const { return index < presets.size() && favorites.count (favoriteKey (presets[index])) > 0; }
    void toggleFavorite (size_t index)
    {
        if (index >= presets.size()) return;
        const auto key = favoriteKey (presets[index]);
        if (favorites.erase (key)) favoriteOrder.removeString (key);
        else favorites.insert (key), favoriteOrder.add (key);
        saveFavorites();
    }

    // Índices de los presets favoritos que existen, en el orden del usuario
    std::vector<size_t> favoriteIndices() const
    {
        std::map<juce::String, size_t> byKey;
        for (size_t i = 0; i < presets.size(); ++i)
            byKey.emplace (favoriteKey (presets[i]), i);
        std::vector<size_t> out;
        for (const auto& k : favoriteOrder)
            if (const auto it = byKey.find (k); it != byKey.end())
                out.push_back (it->second);
        return out;
    }

    // Mueve el favorito del preset index delante del favorito del preset before (o al final si before no es favorito)
    void moveFavorite (size_t index, size_t before)
    {
        if (! isFavorite (index) || index == before) return;
        const auto key = favoriteKey (presets[index]);
        favoriteOrder.removeString (key);
        const auto at = isFavorite (before) ? favoriteOrder.indexOf (favoriteKey (presets[before])) : -1;
        favoriteOrder.insert (at, key); // -1: al final
        saveFavorites();
    }

    // Aviso de la última carga (preset de un formato posterior o con ajustes desconocidos); vacío si no hubo nada
    juce::String lastLoadNote;

    // Índice del preset de fábrica con ese nombre; -1 si no existe
    int factoryIndex (const juce::String& name) const
    {
        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].factory && presets[i].name == name)
                return (int) i;
        return -1;
    }

    int currentIndex() const
    {
        const auto name = apvts.state.getProperty (nameProp).toString();
        const bool factory = apvts.state.getProperty (factoryProp);

        for (size_t i = 0; i < presets.size(); ++i)
            if (presets[i].factory == factory && presets[i].name == name)
                return (int) i;
        return -1;
    }

    bool currentIsUser() const
    {
        const auto i = currentIndex();
        return i >= 0 && ! presets[(size_t) i].factory;
    }

    // Hilo de mensajes. Toda la carga es un solo paso de deshacer (onBulkChange true al empezar, false al terminar)
    juce::Result load (int index)
    {
        if (onBulkChange) onBulkChange (true);
        const auto result = loadInternal (index);
        if (onBulkChange) onBulkChange (false);
        return result;
    }

    std::function<void (bool begin)> onBulkChange;

private:
    juce::Result loadInternal (int index)
    {
        const auto& preset = presets[(size_t) index];
        lastLoadNote = {};
        auto target = targetValues (preset, &lastLoadNote);
        if (! target)
            return juce::Result::fail ("Cannot read preset file:\n" + preset.file.getFullPathName());

        for (auto* p : rangedParameters())
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost ((*target)[p->getParameterID()]);
            p->endChangeGesture();
        }

        setCurrent (preset);
        apvts.state.setProperty (playHintProp, preset.playHint, nullptr);
        apvts.state.setProperty (playLowProp, preset.playLow, nullptr);
        apvts.state.setProperty (playHighProp, preset.playHigh, nullptr);
        // Propiedades de texto (macros por frame, Dsp/FrameMacro.h): las del preset; si no las trae, vacías
        for (const auto* prop : textProperties())
        {
            juce::String value;
            if (preset.factory)
            {
                for (const auto& f : factoryPresets())
                    if (f.name == preset.name)
                        for (const auto& [id, v] : f.properties)
                            if (id == prop) value = v;
            }
            else if (const auto xml = juce::XmlDocument::parse (preset.file); xml != nullptr)
            {
                value = xml->getStringAttribute (prop);
            }
            apvts.state.setProperty (prop, value, nullptr);
        }
        markClean();


        // Ficheros del preset (sample, wavetable, IR). Los presets de usuario anteriores a la 0.3 no tienen estos atributos:
        // entonces no se tocan los ficheros cargados. Los de fábrica quitan los dos samples del Synth (no llevan ninguno) y
        // no tocan wavetable ni IR. El de Osc 2 en modo Sample no se hereda nunca: sin el atributo
        // (formato < 3) se quita, para que un preset no se guarde con el fichero de otro. El de Osc 3 (1.0.1), igual: sin el
        // atributo (presets anteriores), sin fichero. La tabla de Osc 3 sin el atributo no se toca (osc3WtTable ausente vuelve
        // a Basic Shapes, que no usa fichero)
        if (! preset.factory)
            if (const auto xml = juce::XmlDocument::parse (preset.file); xml != nullptr && xml->hasAttribute ("ansUser"))
                apvts.state.setProperty ("ansUser", xml->getStringAttribute ("ansUser"), nullptr);
        if (onPresetFiles)
        {
            if (preset.factory)
            {
                for (const auto& f : factoryPresets())
                    if (f.name == preset.name)
                        onPresetFiles (f.samplePath, std::nullopt, std::nullopt, f.osc2SamplePath, std::nullopt, {}, juce::String(), std::nullopt);
            }
            else if (const auto xml = juce::XmlDocument::parse (preset.file); xml != nullptr)
            {
                // Tabla de Osc 2: su atributo; en un formato < 4 Osc 2 leía la de la fuente Wavetable (mismo fichero)
                const char* osc2Table = xml->hasAttribute (osc2WavetableAttribute) ? osc2WavetableAttribute
                                      : xml->getIntAttribute (formatAttribute, 1) < 4 && xml->hasAttribute (wavetableAttribute) ? wavetableAttribute : nullptr;
                // Samples de los motores de consola: su atributo; en un formato < 6, el motor del preset usaba el de la fuente Sample
                EngineSamplePaths consoles;
                for (size_t k = 0; k < 4; ++k)
                    if (xml->hasAttribute (engineSamples()[k].attribute))
                        consoles[k] = xml->getStringAttribute (engineSamples()[k].attribute);
                if (const auto k = engineSampleFor (xml->getIntAttribute ("engine", 0));
                    xml->getIntAttribute (formatAttribute, 1) < 6 && k >= 0 && xml->getStringAttribute (sampleAttribute).isNotEmpty())
                    consoles[(size_t) k] = xml->getStringAttribute (sampleAttribute);
                onPresetFiles (xml->hasAttribute (sampleAttribute) ? std::optional<juce::String> (xml->getStringAttribute (sampleAttribute)) : std::nullopt,
                               xml->hasAttribute (wavetableAttribute) ? std::optional<juce::String> (xml->getStringAttribute (wavetableAttribute)) : std::nullopt,
                               xml->hasAttribute (cabIrAttribute) ? std::optional<juce::String> (xml->getStringAttribute (cabIrAttribute)) : std::nullopt,
                               xml->getStringAttribute (osc2SampleAttribute),
                               osc2Table != nullptr ? std::optional<juce::String> (xml->getStringAttribute (osc2Table)) : std::nullopt, consoles,
                               xml->getStringAttribute (osc3SampleAttribute),
                               xml->hasAttribute (osc3WavetableAttribute) ? std::optional<juce::String> (xml->getStringAttribute (osc3WavetableAttribute))
                                                                          : std::nullopt);
            }
        }
        snapshotFiles(); // tras onPresetFiles (síncrono): las rutas que deja el preset, también las de ficheros que faltan
        return juce::Result::ok();
    }

    // Rutas de los ficheros del preset (propiedades de estado con el mismo nombre que su atributo en el .xml): samples de la
    // fuente Sample, Osc 2, Osc 3 y los motores de consola, wavetables de la fuente, Osc 2 y Osc 3, e IR de pantalla. No son
    // parámetros: isDirty las compara aparte para que cargar, cambiar o quitar un fichero marque el "*" y permita Save
    std::vector<juce::String> filePathProperties() const
    {
        std::vector<juce::String> ids { sampleAttribute, osc2SampleAttribute, osc3SampleAttribute, wavetableAttribute,
                                        osc2WavetableAttribute, osc3WavetableAttribute, cabIrAttribute };
        for (const auto& e : engineSamples())
            ids.push_back (e.attribute);
        return ids;
    }
    void snapshotFiles()
    {
        fileSnapshot.clear();
        for (const auto& id : filePathProperties())
            fileSnapshot[id] = apvts.state.getProperty (juce::Identifier (id)).toString();
    }

public:
    // Propiedades de estado de texto que forman parte de un preset: macros por frame de cada motor de consola,
    // asignaciones y nombres de las macros de interpretación (Params/PerfMacros.h) y la voz DX7 cargada de un .syx
    // ("n|NOMBRE", pantalla del FM 6-Op; vacía en los de fábrica)
    // (array estático: un std::initializer_list devuelto por valor apuntaría a un array temporal ya destruido)
    // y el orden de la cadena de efectos ("fxOrder"; vacío = el de siempre) y la receta de Math Models > Modal ("modalRecipe";
    // vacía = Soft Key, Engines/SynthData.h)
    static const std::array<const char*, 8>& textProperties()
    {
        static const std::array<const char*, 8> list { "chipMacros", "mdMacros", "snesMacros", "perfMacros", "perfMacroNames", "dx7Voice", "fxOrder",
                                                       "modalRecipe" };
        return list;
    }

    // Lo instala el procesador: aplica las rutas de fichero de un preset (hilo de mensajes). nullopt = no tocar ese fichero;
    // osc2SamplePath y osc3SamplePath siempre llegan (vacías = sin fichero). engineSamples: los de 8-Bit/DPCM, Mega Drive, SNES y N64
    using EngineSamplePaths = std::array<std::optional<juce::String>, 4>;
    std::function<void (std::optional<juce::String> samplePath, std::optional<juce::String> wavetablePath,
                        std::optional<juce::String> cabIrPath, juce::String osc2SamplePath,
                        std::optional<juce::String> osc2WavetablePath, EngineSamplePaths engineSamples,
                        juce::String osc3SamplePath, std::optional<juce::String> osc3WavetablePath)> onPresetFiles;

    // Sample propio de los motores de consola: atributo del preset (= propiedad del estado del procesador), motor que lo usa y
    // prefijo de sus parámetros de región (el DPCM no tenía región: no se copia)
    struct EngineSample { const char* attribute; int engine; const char* params; };
    static const std::array<EngineSample, 4>& engineSamples()
    {
        static const std::array<EngineSample, 4> list { { { "dpcmSamplePath", 7, nullptr }, { "mdSamplePath", 10, "mdSample" },
                                                          { "snesSamplePath", 11, "snesSample" }, { "n64SamplePath", 12, "n64Sample" } } };
        return list;
    }
    // Antes de tener sample propio (presets de formato < 6, proyectos sin la marca engineSamples), el motor de consola del preset
    // usaba el de la fuente Sample: índice en engineSamples() del motor 'engine', o -1
    static int engineSampleFor (int engine)
    {
        for (int k = 0; k < 4; ++k)
            if (engineSamples()[(size_t) k].engine == engine)
                return k;
        return -1;
    }

    juce::Result validateName (const juce::String& name) const
    {
        if (name.isEmpty())
            return juce::Result::fail ("The name cannot be empty.");
        if (juce::File::createLegalFileName (name) != name)
            return juce::Result::fail ("The name contains characters not allowed in file names.");
        for (const auto& f : factoryPresets())
            if (f.name.equalsIgnoreCase (name))
                return juce::Result::fail ("\"" + name + "\" is a factory preset name.");
        return juce::Result::ok();
    }

    bool userPresetExists (const juce::String& name) const { return fileFor (name).existsAsFile(); }

    // Exportar e importar presets de usuario (navegador). Un preset viaja como su .xml tal cual; el banco, como un .zip con
    // los .xml en la raíz. Los ficheros a los que apunta (samples, IR, wavetables) no viajan: el preset guarda sus rutas y, si
    // faltan al cargarlo, se avisa con Locate... / Ignore
    juce::Result exportPreset (size_t index, const juce::File& dest) const
    {
        if (index >= presets.size() || presets[index].factory)
            return juce::Result::fail ("Only user presets can be exported.");
        if (! presets[index].file.copyFileTo (dest))
            return juce::Result::fail ("Could not write " + dest.getFullPathName() + ".");
        return juce::Result::ok();
    }

    juce::Result exportAll (const juce::File& dest) const
    {
        juce::ZipFile::Builder zip;
        int n = 0;
        for (const auto& p : presets)
            if (! p.factory && p.file.existsAsFile())
                zip.addFile (p.file, 9, p.file.getFileName()), ++n;
        if (n == 0)
            return juce::Result::fail ("There are no user presets to export.");
        dest.deleteFile(); // FileOutputStream añade al final de un fichero existente (el selector ya confirmó sobrescribir)
        juce::FileOutputStream out (dest);
        if (! out.openedOk() || ! zip.writeToStream (out, nullptr))
            return juce::Result::fail ("Could not write " + dest.getFullPathName() + ".");
        return juce::Result::ok();
    }

    // Presets legibles de un .xml o de un .zip (solo los .xml del archivo, sin sus carpetas): etiqueta KoniSynthPreset, nombre
    // válido (validateName: no el de un preset de fábrica) y hasta 4 MB cada uno (un preset ocupa unos 30 kB). La versión de
    // formato no se comprueba aquí: al cargarlo, migrateValues lo trae al actual o avisa si es de una versión posterior
    struct Incoming { juce::String name, text; };
    std::vector<Incoming> readImport (const juce::File& f, juce::StringArray& rejected) const
    {
        constexpr juce::int64 maxBytes = 4 << 20;
        std::vector<Incoming> out;
        const auto accept = [&] (const juce::String& fileName, const juce::String& text)
        {
            const auto name = fileName.upToLastOccurrenceOf (".", false, false);
            const auto xml = juce::XmlDocument::parse (text);
            if (xml == nullptr || ! xml->hasTagName (xmlTag))
                rejected.add (fileName + ": not a KoniSynth preset");
            else if (auto r = validateName (name); r.failed())
                rejected.add (fileName + ": " + r.getErrorMessage());
            else
                out.push_back ({ name, text });
        };
        if (f.hasFileExtension ("zip"))
        {
            juce::ZipFile zip (f);
            for (int i = 0; i < zip.getNumEntries(); ++i)
            {
                const auto* e = zip.getEntry (i);
                const auto fileName = e->filename.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
                if (! fileName.endsWithIgnoreCase (".xml"))
                    continue;
                if (e->uncompressedSize > maxBytes) { rejected.add (fileName + ": too large"); continue; }
                if (std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i)); in != nullptr)
                    accept (fileName, in->readEntireStreamAsString());
                else
                    rejected.add (fileName + ": could not be read");
            }
        }
        else if (f.getSize() > maxBytes)
            rejected.add (f.getFileName() + ": too large");
        else
            accept (f.getFileName(), f.loadFileAsString());
        return out;
    }

    // Escribe un preset importado en la carpeta de usuario (sobrescribe si existe: la confirmación la pide la UI)
    juce::Result writeImported (const Incoming& p)
    {
        userDirectory().createDirectory();
        if (! fileFor (p.name).replaceWithText (p.text))
            return juce::Result::fail ("Could not write " + fileFor (p.name).getFullPathName() + ".");
        return juce::Result::ok();
    }

    // Crea o sobrescribe (la confirmación de sobrescritura la pide la UI).
    juce::Result saveAs (const juce::String& name, const juce::String& category)
    {
        if (auto r = validateName (name); r.failed())
            return r;

        // La indicación y el rango del estado (los del preset actual, editados o no) pasan al nuevo
        const auto file = fileFor (name);
        if (auto r = write (file, category); r.failed())
            return r;

        refresh();
        setCurrent ({ name, false, file, category });
        markClean();
        return juce::Result::ok();
    }

    // Sobrescribe el preset de usuario actual con los valores actuales.
    juce::Result save()
    {
        if (! currentIsUser())
            return juce::Result::fail ("Only user presets can be overwritten.");

        const auto& current = presets[(size_t) currentIndex()];
        if (auto r = write (current.file, current.unknownCategory.isNotEmpty() ? current.unknownCategory : current.category); r.failed())
            return r;

        refresh(); // la indicación y el rango guardados pasan a la lista (navegador, display)
        markClean();
        return juce::Result::ok();
    }

    juce::Result rename (const juce::String& newName)
    {
        if (! currentIsUser())
            return juce::Result::fail ("Only user presets can be renamed.");
        if (auto r = validateName (newName); r.failed())
            return r;

        const auto old = presets[(size_t) currentIndex()];
        const auto target = fileFor (newName);

        // En Windows los nombres no distinguen mayúsculas: "pad" -> "Pad" es el mismo fichero.
        if (target.existsAsFile() && ! newName.equalsIgnoreCase (old.name))
            return juce::Result::fail ("A user preset named \"" + newName + "\" already exists.");
        if (! old.file.moveFileTo (target))
            return juce::Result::fail ("Cannot rename:\n" + old.file.getFullPathName());

        // El favorito (y su puesto en el orden propio) sigue al preset: la clave lleva el nombre
        const auto oldKey = favoriteKey (old), newKey = juce::String ("U:") + newName;
        if (oldKey != newKey && favorites.erase (oldKey) > 0)
        {
            favorites.insert (newKey);
            const auto at = favoriteOrder.indexOf (oldKey);
            if (at >= 0) favoriteOrder.set (at, newKey); else favoriteOrder.add (newKey);
            saveFavorites();
        }

        refresh();
        setCurrent ({ newName, false, target, old.category });
        return juce::Result::ok();
    }

    juce::Result remove()
    {
        if (! currentIsUser())
            return juce::Result::fail ("Only user presets can be deleted.");

        const auto file = presets[(size_t) currentIndex()].file;
        if (! file.deleteFile())
            return juce::Result::fail ("Cannot delete:\n" + file.getFullPathName());

        refresh();
        setCurrent ({}); // sin preset; los parámetros se quedan como están
        return juce::Result::ok();
    }

    // true si algún parámetro difiere del preset cargado (tolerancia 1e-4 en escala 0..1).
    bool isDirty()
    {
        const auto index = currentIndex();
        if (index < 0)
            return false;

        if (! snapshotValid) // estado restaurado por el host: recalcula la referencia sin aplicarla
        {
            auto target = targetValues (presets[(size_t) index]);
            if (! target)
                return false;
            snapshot = std::move (*target);
            textSnapshot.clear();
            snapshotFiles(); // sin referencia del preset para los ficheros: los del estado restaurado
            snapshotValid = true;
        }

        for (auto* p : rangedParameters())
            if (std::abs (p->getValue() - snapshot[p->getParameterID()]) > 1.0e-4f)
                return true;
        for (const auto& [prop, text] : textSnapshot) // macros editadas (sin referencia tras restaurar el host: no se comparan)
            if (apvts.state.getProperty (prop).toString() != text)
                return true;
        for (const auto& [prop, path] : fileSnapshot) // sample, wavetable o IR cargado, cambiado o quitado
            if (apvts.state.getProperty (juce::Identifier (prop)).toString() != path)
                return true;
        return false;
    }

    void invalidateSnapshot() { snapshotValid = false; } // seguro desde cualquier hilo

private:
    using Values = std::map<juce::String, float>; // id -> valor normalizado 0..1

    static inline const juce::Identifier nameProp { "presetName" }, factoryProp { "presetFactory" };
    static constexpr const char* xmlTag = "KoniSynthPreset";
    static constexpr const char* categoryAttribute = "presetCategory"; // no es un ID de parámetro: targetValues lo ignora
    static constexpr const char* playHintAttribute = "playHint";       // texto: tampoco es un parámetro
    static constexpr const char* playLowAttribute = "playLow", * playHighAttribute = "playHigh"; // notas MIDI del rango
    static constexpr const char* formatAttribute = "formatVersion", * versionAttribute = "pluginVersion";

    std::set<juce::String> favorites; // búsqueda (isFavorite se llama por fila y por recuento de filtro)
    juce::StringArray favoriteOrder;  // las mismas claves, en el orden del usuario
    static inline const juce::String favoritesOrderedMark { "#ordered" };
    static juce::String favoriteKey (const Preset& p) { return (p.factory ? "F:" : "U:") + p.name; }
    // En config.settings (clave favorites), con el mismo texto que el favorites.txt de versiones anteriores (marca de orden y
    // una clave por línea), que se migra y se borra
    void loadFavorites()
    {
        juce::StringArray lines;
        lines.addLines (configText ("favorites", "favorites.txt"));
        const auto ordered = lines.size() > 0 && lines[0].trim() == favoritesOrderedMark;
        for (const auto& l : lines)
            if (const auto k = l.trim(); k.isNotEmpty() && ! k.startsWithChar ('#') && favorites.insert (k).second)
                favoriteOrder.add (k);
        // Fichero anterior al orden propio (sin la marca): el orden que tenían las teclas 1-9, por nombre (natural)
        if (! ordered)
            std::stable_sort (favoriteOrder.begin(), favoriteOrder.end(), [] (const juce::String& a, const juce::String& b)
                              { return a.substring (2).compareNatural (b.substring (2)) < 0; });
    }
    void saveFavorites() const
    {
        setConfigText ("favorites", favoritesOrderedMark + "\n" + favoriteOrder.joinIntoString ("\n"));
    }
    // Mismos nombres que las propiedades de estado del procesador (samplePathProp, osc2SamplePathProp, osc3SamplePathProp, wtPathProp,
    // osc2WtPathProp, osc3WtPathProp)
    static constexpr const char* sampleAttribute = "samplePath";
    static constexpr const char* osc2SampleAttribute = "osc2SamplePath", * osc3SampleAttribute = "osc3SamplePath";
    static constexpr const char* wavetableAttribute = "wtPath", * osc2WavetableAttribute = "osc2WtPath", * osc3WavetableAttribute = "osc3WtPath";
    static constexpr const char* cabIrAttribute = "cabIrPath";

    // Parámetros que forman parte de un preset. Quedan fuera los de interpretación (el pad del theremin):
    // tocar no debe marcar el preset como modificado ni guardarse en él.
    juce::Array<juce::RangedAudioParameter*> rangedParameters() const
    {
        juce::Array<juce::RangedAudioParameter*> result;
        for (auto* p : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->getParameterID() != "thereminPitch" && rp->getParameterID() != "thereminVolume")
                    result.add (rp);
        return result;
    }

    juce::File fileFor (const juce::String& name) const { return userDirectory().getChildFile (name + ".xml"); }

    void setCurrent (const Preset& p)
    {
        apvts.state.setProperty (nameProp, p.name, nullptr);
        apvts.state.setProperty (factoryProp, p.factory, nullptr);
    }

    void markClean()
    {
        snapshot.clear();
        for (auto* p : rangedParameters())
            snapshot[p->getParameterID()] = p->getValue();
        textSnapshot.clear();
        for (const auto* prop : textProperties())
            textSnapshot[prop] = apvts.state.getProperty (prop).toString();
        for (const auto& prop : { playHintProp, playLowProp, playHighProp }) // editar cómo tocarlo también marca el "*"
            textSnapshot[prop.toString()] = apvts.state.getProperty (prop).toString();
        snapshotFiles(); // Save / Save As: los ficheros guardados. En una carga, loadInternal la repite tras restaurarlos
        snapshotValid = true;
    }

    // Migración de un formato anterior (valores en unidades reales, por ID). Un paso por versión; cada uno deja el formato
    // siguiente. Para añadir uno: subir formatVersion y añadir aquí "if (from < N) { ... }"
    static void migrateValues (std::map<juce::String, float>& real, int from)
    {
        if (from < 2) // 1 -> 2: presets anteriores a "engine": thereminOn = 1 -> engine = Theremin
            if (real.count ("engine") == 0 && real.count ("thereminOn") != 0)
                real["engine"] = real["thereminOn"] > 0.5f ? 1.0f : 0.0f;
        // 2 -> 3: ningún valor cambia. osc2Wave no podía valer Sample (5) y los parámetros osc2Sample* ausentes toman su valor
        // por defecto; el fichero de Osc 2 (osc2SamplePath) ausente se quita al cargar (loadInternal)
        if (from < 4) // 3 -> 4: Osc 2 leía la tabla de la fuente Wavetable; su tabla propia empieza siendo la misma.
        {             // osc2Wave: Wavetable (4) y Sample (5) pasan al final (10, 11) y las mezclas 6..11 a 4..9
            if (real.count ("osc2WtTable") == 0)
                real["osc2WtTable"] = real.count ("wtTable") != 0 ? real["wtTable"] : 0.0f;
            if (const auto it = real.find ("osc2Wave"); it != real.end())
                it->second = (float) osc2WaveFromFormat3 (juce::roundToInt (it->second));
        }
        if (from < 5) // 4 -> 5: KSL del OPL en dB/oct; con OPL2/OPL3 se intercambian 1 y 2 para que suene igual
            swapOplKslIfRegisterOrder (real);
        if (from < 6) // 5 -> 6: el motor de consola del preset toma la región de la fuente Sample para su sample propio
            copySampleRegionToEngine ([&real] (const juce::String& id, float def) { const auto it = real.find (id); return it != real.end() ? it->second : def; },
                                      [&real] (const juce::String& id, float v) { real[id] = v; });
        if (from < 7) // 6 -> 7: subOctave + Semi ±24 -> osc3Octave + Semi ±12, con la misma afinación
        {
            const auto get = [&real] (const char* id, float def) { const auto it = real.find (id); return it != real.end() ? it->second : def; };
            const auto [octave, semi] = osc3FromSub (juce::roundToInt (get ("subOctave", 0.0f)), juce::roundToInt (get ("osc3Semi", 0.0f)));
            real["osc3Octave"] = (float) octave;
            real["osc3Semi"] = (float) semi;
            real.erase ("subOctave");
        }
        if (from < 8) // 7 -> 8: Wavetable / Sample de Osc 2 y Osc 3, de forma de onda a fuente con su nivel
            splitOscFamilies ([&real] (const char* id, float def) { const auto it = real.find (id); return it != real.end() ? it->second : def; },
                              [&real] (const char* id, float v) { real[id] = v; });
    }

public:
    // Formato 7 -> 8: si osc2Wave (u osc3Wave) era Wavetable (10) o Sample (11), esa fuente toma el nivel del oscilador, el
    // oscilador queda a 0 con su forma por defecto (Saw / Square) y, si era el modulador del Complex (Osc 2 / Osc 3), el
    // modulador pasa a la fuente (Osc 2 WT 4, Osc 2 Sample 5, Osc 3 WT 6, Osc 3 Sample 7): suena igual. get (id, por defecto) y
    // set (id, valor), en valores reales. También lo usa KoniSynthProcessor::setStateInformation con los proyectos anteriores
    template <typename Get, typename Set>
    static void splitOscFamilies (Get&& get, Set&& set)
    {
        struct Family { const char* wave; float defaultWave; const char* level; const char* wtLevel; const char* sampleLevel;
                        int modSource, wtModSource; };
        for (const auto& f : { Family { "osc2Wave", 2.0f, "osc2Level", "osc2WtLevel", "osc2SampleLevel", 1, 4 },
                               Family { "osc3Wave", 3.0f, "subLevel", "osc3WtLevel", "osc3SampleLevel", 2, 6 } })
        {
            const auto wave = juce::roundToInt (get (f.wave, f.defaultWave));
            if (wave != legacyWaveTable && wave != legacyWaveSample)
                continue;
            const auto sampled = wave == legacyWaveSample;
            set (sampled ? f.sampleLevel : f.wtLevel, get (f.level, 0.0f)); // osc2Level y subLevel valen 0 por defecto
            set (f.level, 0.0f);
            set (f.wave, f.defaultWave);
            if (juce::roundToInt (get ("complexModSource", 0.0f)) == f.modSource)
                set ("complexModSource", (float) (f.wtModSource + (sampled ? 1 : 0)));
        }
    }
private:

public:
    // Osc 3 hasta la 1.0.0-beta (subOctave 0 = -1 oct, 1 = -2 oct; Semi -24..+24, lo que pasara se acotaba ahí) -> Oct -2..+2 y
    // Semi -12..+12, misma afinación: total = base + Semi semitonos; Oct = total/12 redondeado (acotado a ±2) y Semi el resto.
    // Exacto de -36 a +12 st, todo lo que cabía con -1 oct; con -2 oct y Semi < -12 (de -48 a -37 st) se queda en -36.
    // Ejemplos: (-1 oct, +24) -> (+1, 0); (-1 oct, -24) -> (-2, -12); (-1 oct, +19) -> (+1, -5); (-1 oct, -5) -> (-1, -5).
    // También lo usa KoniSynthProcessor::setStateInformation con los proyectos anteriores
    static std::pair<int, int> osc3FromSub (int subOctave, int semi)
    {
        const auto total = (subOctave == 1 ? -24 : -12) + juce::jlimit (-24, 24, semi);
        const auto octave = juce::jlimit (-2, 2, (int) std::lround (total / 12.0));
        return { octave, juce::jlimit (-12, 12, total - 12 * octave) };
    }
private:

public:
    // Región (Root, Start, Length, Loop) de la fuente Sample -> la del sample propio del motor de consola del preset (presets de
    // formato < 6 y proyectos sin la marca engineSamples). get (id, por defecto) y set (id, valor), en valores reales
    template <typename Get, typename Set>
    static void copySampleRegionToEngine (Get&& get, Set&& set)
    {
        const auto k = engineSampleFor (juce::roundToInt (get ("engine", 0.0f)));
        if (k < 0 || engineSamples()[(size_t) k].params == nullptr)
            return;
        const juce::String to (engineSamples()[(size_t) k].params);
        set (to + "Root", get ("sampleRoot", 60.0f));
        set (to + "Start", get ("sampleStart", 0.0f));
        set (to + "Length", get ("sampleLength", 0.0f));
        set (to + "Loop", get ("sampleLoop", 0.0f));
    }
private:

public:
    // KSL de los operadores OPL guardado como valor de registro (presets de formato < 5 y proyectos sin el atributo kslDb):
    // en OPL2/OPL3 (oplModel 0, 1) el registro 1 era 3 dB/oct y el 2, 1,5 dB/oct; en dB/oct van al revés. OPLL y VRC7 no
    // cambian (su registro ya era monótono). get (id, por defecto) lee y set (id, valor) escribe el valor real de un parámetro
    template <typename Get, typename Set>
    static void swapOplKsl (Get&& get, Set&& set)
    {
        if (juce::roundToInt (get ("engine", 0.0f)) != 13 || juce::roundToInt (get ("oplModel", 0.0f)) > 1)
            return;
        for (int op = 1; op <= 4; ++op)
        {
            const auto id = "opl" + juce::String (op) + "Ksl";
            if (const auto k = juce::roundToInt (get (id, 0.0f)); k == 1 || k == 2)
                set (id, (float) (3 - k));
        }
    }
private:
    static void swapOplKslIfRegisterOrder (std::map<juce::String, float>& real)
    {
        swapOplKsl ([&real] (const juce::String& id, float def) { const auto it = real.find (id); return it != real.end() ? it->second : def; },
                    [&real] (const juce::String& id, float v) { real[id] = v; });
    }

public:
    // osc2Wave del formato 3 (Sine, Triangle, Saw, Square, Wavetable, Sample, mezclas) al actual (mezclas, Wavetable, Sample).
    // También lo usa KoniSynthProcessor::setStateInformation con los proyectos anteriores
    static int osc2WaveFromFormat3 (int v)
    {
        return v == 4 ? 10 : v == 5 ? 11 : v >= 6 ? v - 2 : v;
    }
private:

    // Valores normalizados que deja el preset; los ausentes vuelven al valor por defecto. note: aviso para la UI (formato
    // posterior o atributos que no son parámetros conocidos)
    std::optional<Values> targetValues (const Preset& preset, juce::String* note = nullptr) const
    {
        std::map<juce::String, float> real;

        if (preset.factory)
        {
            for (const auto& f : factoryPresets())
                if (f.name == preset.name)
                    for (const auto& [id, v] : f.values)
                        real[id] = v;
        }
        else
        {
            const auto xml = juce::XmlDocument::parse (preset.file);
            if (xml == nullptr || ! xml->hasTagName (xmlTag))
                return std::nullopt;

            for (int i = 0; i < xml->getNumAttributes(); ++i)
                real[xml->getAttributeName (i)] = xml->getAttributeValue (i).getFloatValue();

            const auto version = xml->getIntAttribute (formatAttribute, 1);
            migrateValues (real, version);

            if (note != nullptr)
            {
                // Atributos que no son parámetros ni propiedades conocidas: de una versión posterior o renombrados
                juce::StringArray known { formatAttribute, versionAttribute, categoryAttribute, playHintAttribute, playLowAttribute, playHighAttribute,
                                          sampleAttribute, osc2SampleAttribute, osc3SampleAttribute, wavetableAttribute, osc2WavetableAttribute,
                                          osc3WavetableAttribute, cabIrAttribute, "ansUser", "thereminOn", "subOctave" };
                for (const auto& e : engineSamples()) known.add (e.attribute);
                for (const auto* prop : textProperties()) known.add (prop);
                for (auto* p : rangedParameters()) known.add (p->getParameterID());
                juce::StringArray unknown;
                for (int i = 0; i < xml->getNumAttributes(); ++i)
                    if (! known.contains (xml->getAttributeName (i)))
                        unknown.add (xml->getAttributeName (i));
                if (version > formatVersion)
                    *note << "This preset was saved by a newer KoniSynth (" << xml->getStringAttribute (versionAttribute, "?")
                          << ", preset format " << version << "; this version reads up to " << formatVersion << "). ";
                if (! unknown.isEmpty())
                    *note << juce::String (unknown.size()) << " setting" << (unknown.size() == 1 ? "" : "s")
                          << " not known by this version " << (unknown.size() == 1 ? "was" : "were") << " ignored: " << unknown.joinIntoString (", ", 0, 8)
                          << (unknown.size() > 8 ? "..." : "") << ".";
            }
        }

        Values out;
        for (auto* p : rangedParameters())
        {
            const auto it = real.find (p->getParameterID());
            out[p->getParameterID()] = it != real.end() ? p->convertTo0to1 (it->second) : p->getDefaultValue();
        }
        return out;
    }

    juce::Result write (const juce::File& file, const juce::String& category) const
    {
        if (auto r = file.getParentDirectory().createDirectory(); r.failed())
            return r;

        juce::XmlElement xml (xmlTag);
        xml.setAttribute (formatAttribute, formatVersion);
        xml.setAttribute (versionAttribute, JucePlugin_VersionString);
        xml.setAttribute (categoryAttribute, category);
        // Cómo tocarlo: del estado (lo que trajo el preset o lo editado en el teclado); sin indicación ni rango, no se escribe
        if (const auto hint = apvts.state.getProperty (playHintProp).toString(); hint.isNotEmpty())
            xml.setAttribute (playHintAttribute, hint);
        const int low = apvts.state.getProperty (playLowProp, -1), high = apvts.state.getProperty (playHighProp, -1);
        if (low >= 0 && high >= low)
        {
            xml.setAttribute (playLowAttribute, low);
            xml.setAttribute (playHighAttribute, high);
        }
        // Rutas de los ficheros (no son parámetros: van en el estado). Vacías = sin fichero
        xml.setAttribute (sampleAttribute, apvts.state.getProperty (sampleAttribute).toString());
        xml.setAttribute (osc2SampleAttribute, apvts.state.getProperty (osc2SampleAttribute).toString());
        xml.setAttribute (osc3SampleAttribute, apvts.state.getProperty (osc3SampleAttribute).toString());
        for (const auto& e : engineSamples())
            xml.setAttribute (e.attribute, apvts.state.getProperty (e.attribute).toString());
        xml.setAttribute (wavetableAttribute, apvts.state.getProperty (wavetableAttribute).toString());
        xml.setAttribute (osc2WavetableAttribute, apvts.state.getProperty (osc2WavetableAttribute).toString());
        xml.setAttribute (osc3WavetableAttribute, apvts.state.getProperty (osc3WavetableAttribute).toString());
        xml.setAttribute (cabIrAttribute, apvts.state.getProperty (cabIrAttribute).toString());
        if (const auto ans = apvts.state.getProperty ("ansUser").toString(); ans.isNotEmpty())
            xml.setAttribute ("ansUser", ans); // partitura del ANS dibujada por el usuario
        for (const auto* prop : textProperties())
            if (const auto text = apvts.state.getProperty (prop).toString(); text.isNotEmpty())
                xml.setAttribute (prop, text);
        for (auto* p : rangedParameters())
            xml.setAttribute (p->getParameterID(), p->convertFrom0to1 (p->getValue()));

        return xml.writeTo (file) ? juce::Result::ok()
                                  : juce::Result::fail ("Cannot write:\n" + file.getFullPathName());
    }

    APVTS& apvts;
    std::vector<Preset> presets;
    Values snapshot;
    std::atomic<bool> snapshotValid { false };
    std::map<juce::String, juce::String> textSnapshot;
    std::map<juce::String, juce::String> fileSnapshot; // rutas de los ficheros del preset (filePathProperties)
};
