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
    };

    // Cómo tocar el preset actual: propiedades del estado (no son parámetros). Las fija la carga de un preset, se pueden
    // editar desde el teclado en pantalla (cuentan como cambio sin guardar, "*") y las escribe Save / Save As
    static inline const juce::Identifier playHintProp { "playHint" }, playLowProp { "playLow" }, playHighProp { "playHigh" };

    // 1: hasta la 0.6 (sin atributo). 2: formatVersion y pluginVersion; thereminOn ya migrado a engine. 3: Osc 2 en modo Sample, con su
    // fichero (atributo osc2SamplePath). 4: tabla propia de Osc 2 en modo Wavetable (osc2WtTable, fichero en osc2WtPath) y
    // osc2Wave con Wavetable y Sample al final de la lista
    static constexpr int formatVersion = 4;

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
        }

        auto files = userDirectory().findChildFiles (juce::File::findFiles, false, "*.xml");
        files.sort();
        for (const auto& f : files)
        {
            // Categoría del preset de usuario: atributo presetCategory (sin él, presets anteriores -> "Init")
            juce::String category ("Init"), hint;
            int engine = 0, low = -1, high = -1;
            if (const auto xml = juce::XmlDocument::parse (f); xml != nullptr && xml->hasTagName (xmlTag))
            {
                category = xml->getStringAttribute (categoryAttribute, "Init");
                hint = xml->getStringAttribute (playHintAttribute);
                low = xml->getIntAttribute (playLowAttribute, -1);
                high = xml->getIntAttribute (playHighAttribute, -1);
                engine = juce::roundToInt (xml->hasAttribute ("engine") ? xml->getDoubleAttribute ("engine")
                                                                        : (xml->getDoubleAttribute ("thereminOn") > 0.5 ? 1.0 : 0.0));
            }
            if (! presetCategories().contains (category))
                category = "Init";
            presets.push_back ({ f.getFileNameWithoutExtension(), false, f, category, engine, hint, low, high });
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
        // entonces no se tocan los ficheros cargados; los de fábrica no usan ficheros. El de Osc 2 en modo Sample no se hereda
        // nunca: sin el atributo (formato < 3 o de fábrica) se quita, para que un preset no se guarde con el fichero de otro
        if (! preset.factory)
            if (const auto xml = juce::XmlDocument::parse (preset.file); xml != nullptr && xml->hasAttribute ("ansUser"))
                apvts.state.setProperty ("ansUser", xml->getStringAttribute ("ansUser"), nullptr);
        if (onPresetFiles)
        {
            if (preset.factory)
                onPresetFiles (std::nullopt, std::nullopt, std::nullopt, juce::String(), std::nullopt);
            else if (const auto xml = juce::XmlDocument::parse (preset.file); xml != nullptr)
            {
                // Tabla de Osc 2: su atributo; en un formato < 4 Osc 2 leía la de la fuente Wavetable (mismo fichero)
                const char* osc2Table = xml->hasAttribute (osc2WavetableAttribute) ? osc2WavetableAttribute
                                      : xml->getIntAttribute (formatAttribute, 1) < 4 && xml->hasAttribute (wavetableAttribute) ? wavetableAttribute : nullptr;
                onPresetFiles (xml->hasAttribute (sampleAttribute) ? std::optional<juce::String> (xml->getStringAttribute (sampleAttribute)) : std::nullopt,
                               xml->hasAttribute (wavetableAttribute) ? std::optional<juce::String> (xml->getStringAttribute (wavetableAttribute)) : std::nullopt,
                               xml->hasAttribute (cabIrAttribute) ? std::optional<juce::String> (xml->getStringAttribute (cabIrAttribute)) : std::nullopt,
                               xml->getStringAttribute (osc2SampleAttribute),
                               osc2Table != nullptr ? std::optional<juce::String> (xml->getStringAttribute (osc2Table)) : std::nullopt);
            }
        }
        return juce::Result::ok();
    }

public:
    // Propiedades de estado de texto que forman parte de un preset: macros por frame de cada motor de consola,
    // asignaciones y nombres de las macros de interpretación (Params/PerfMacros.h) y la voz DX7 cargada de un .syx
    // ("n|NOMBRE", pantalla del FM 6-Op; vacía en los de fábrica)
    // (array estático: un std::initializer_list devuelto por valor apuntaría a un array temporal ya destruido)
    static const std::array<const char*, 6>& textProperties()
    {
        static const std::array<const char*, 6> list { "chipMacros", "mdMacros", "snesMacros", "perfMacros", "perfMacroNames", "dx7Voice" };
        return list;
    }

    // Lo instala el procesador: aplica las rutas de fichero de un preset (hilo de mensajes). nullopt = no tocar ese fichero;
    // osc2SamplePath siempre llega (vacía = sin fichero)
    std::function<void (std::optional<juce::String> samplePath, std::optional<juce::String> wavetablePath,
                        std::optional<juce::String> cabIrPath, juce::String osc2SamplePath,
                        std::optional<juce::String> osc2WavetablePath)> onPresetFiles;

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
        if (auto r = write (current.file, current.category); r.failed())
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
            snapshotValid = true;
        }

        for (auto* p : rangedParameters())
            if (std::abs (p->getValue() - snapshot[p->getParameterID()]) > 1.0e-4f)
                return true;
        for (const auto& [prop, text] : textSnapshot) // macros editadas (sin referencia tras restaurar el host: no se comparan)
            if (apvts.state.getProperty (prop).toString() != text)
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
    static juce::File favoritesFile() { return userDirectory().getParentDirectory().getChildFile ("favorites.txt"); }
    static juce::String favoriteKey (const Preset& p) { return (p.factory ? "F:" : "U:") + p.name; }
    void loadFavorites()
    {
        juce::StringArray lines;
        lines.addLines (favoritesFile().loadFileAsString());
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
        favoritesFile().getParentDirectory().createDirectory();
        favoritesFile().replaceWithText (favoritesOrderedMark + "\n" + favoriteOrder.joinIntoString ("\n") + "\n");
    }
    // Mismos nombres que las propiedades de estado del procesador (samplePathProp, osc2SamplePathProp, wtPathProp, osc2WtPathProp)
    static constexpr const char* sampleAttribute = "samplePath";
    static constexpr const char* osc2SampleAttribute = "osc2SamplePath";
    static constexpr const char* wavetableAttribute = "wtPath", * osc2WavetableAttribute = "osc2WtPath";
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
                                          sampleAttribute, osc2SampleAttribute, wavetableAttribute, osc2WavetableAttribute, cabIrAttribute, "ansUser", "thereminOn" };
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
        xml.setAttribute (wavetableAttribute, apvts.state.getProperty (wavetableAttribute).toString());
        xml.setAttribute (osc2WavetableAttribute, apvts.state.getProperty (osc2WavetableAttribute).toString());
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
};
