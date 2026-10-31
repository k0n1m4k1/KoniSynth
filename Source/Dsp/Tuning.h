// Se incluye desde KoniSynth.cpp (y desde Bench/KoniBench.cpp para su prueba unitaria). Usa JUCE y el cliente MTS-ESP.
#pragma once
#include <bitset>
#include "../ThirdParty/MTS-ESP/libMTSClient.h"

//==============================================================================
// Microafinación: escalas Scala (.scl + .kbm opcional) y MTS-ESP (ODDSound, cliente 0BSD).
// Los motores calculan su frecuencia de 12-TET como siempre y la multiplican por KoniTuning::ratio (note): 1.0 exacto sin
// afinación (x · 1.0 es exacto en coma flotante: la salida no cambia ni un bit). Prioridad: maestro MTS-ESP conectado (o
// SysEx MTS recibido) > escala Scala cargada > 12-TET.
// Hilos: la tabla Scala se publica con doble búfer (el de mensajes escribe la inactiva y cambia el índice); el hilo de
// audio solo lee. MTS-ESP: las consultas son lecturas de una tabla compartida (aptas para el hilo de audio).
// Instancia actual: KoniTuning::current (thread_local) apunta a la Tuning del procesador mientras dura su processBlock;
// fuera (o en otro hilo) vale nullptr y ratio () devuelve 1.
namespace Scala
{
// Escala: grados en cents (sin el 1/1 implícito; el último es el periodo)
struct Scale { juce::String description; std::vector<double> cents; };

// Mapa de teclado (.kbm). mapSize 0 = lineal (un grado por tecla, periodo = tamaño de la escala); -1 en map = 'x'
struct Mapping
{
    int mapSize = 0, firstNote = 0, lastNote = 127, middleNote = 60, referenceNote = 69, formalOctave = 0;
    double referenceHz = 440.0;
    std::vector<int> map;
};

// Líneas útiles: sin comentarios ('!') ni retornos de carro
inline juce::StringArray lines (const juce::String& text)
{
    juce::StringArray out;
    for (auto line : juce::StringArray::fromLines (text))
        if (! line.trimStart().startsWithChar ('!'))
            out.add (line.trim());
    return out;
}

// Primer token de una línea (lo que sigue a un valor válido se ignora)
inline juce::String token (const juce::String& line)
{
    return line.upToFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf ("\t", false, false);
}

// .scl (huygens-fokker.org/scala/scl_format.html): descripción, número de notas y una línea por grado; con punto =
// cents, si no razón "a/b" o entero "a". Razones negativas o nulas: error
inline juce::String parseScale (const juce::String& text, Scale& out)
{
    // la descripción puede ser una línea vacía: se toma la primera línea no comentario tal cual
    juce::StringArray raw;
    for (auto line : juce::StringArray::fromLines (text))
        if (! line.trimStart().startsWithChar ('!'))
            raw.add (line);
    if (raw.size() < 2)
        return "not a Scala scale (.scl): missing description or note count";
    out = {};
    out.description = raw[0].trim();
    const auto countText = token (raw[1].trim());
    if (! countText.containsOnly ("0123456789") || countText.isEmpty())
        return "not a Scala scale (.scl): bad note count '" + raw[1].trim() + "'";
    const auto count = countText.getIntValue();
    if (count < 1 || count > 1024)
        return "unsupported note count: " + juce::String (count);
    int got = 0;
    for (int i = 2; i < raw.size() && got < count; ++i)
    {
        const auto t = token (raw[i].trim());
        if (t.isEmpty())
            continue;
        double cents = 0.0;
        if (t.containsChar ('.'))
        {
            if (! t.containsOnly ("+-0123456789.") )
                return "bad pitch on degree " + juce::String (got + 1) + ": '" + t + "'";
            cents = t.getDoubleValue();
        }
        else
        {
            const auto num = t.upToFirstOccurrenceOf ("/", false, false), den = t.fromFirstOccurrenceOf ("/", false, false);
            if (! num.containsOnly ("0123456789") || num.isEmpty() || (t.containsChar ('/') && (den.isEmpty() || ! den.containsOnly ("0123456789"))))
                return "bad pitch on degree " + juce::String (got + 1) + ": '" + t + "'";
            const auto a = num.getLargeIntValue(), b = t.containsChar ('/') ? den.getLargeIntValue() : (juce::int64) 1;
            if (a <= 0 || b <= 0)
                return "ratio must be positive on degree " + juce::String (got + 1);
            cents = 1200.0 * std::log2 ((double) a / (double) b);
        }
        out.cents.push_back (cents);
        ++got;
    }
    if (got != count)
        return "the scale says " + juce::String (count) + " notes but has " + juce::String (got);
    return {};
}

// .kbm: tamaño del mapa, primera y última nota, nota central, nota de referencia, su frecuencia, grado de octava formal y
// las entradas (un grado o 'x' por tecla desde la central; las 'x' finales pueden omitirse)
inline juce::String parseMapping (const juce::String& text, Mapping& out)
{
    const auto l = lines (text);
    juce::StringArray values;
    for (auto& line : l)
        if (line.isNotEmpty())
            values.add (token (line));
    if (values.size() < 7)
        return "not a Scala keyboard mapping (.kbm): needs 7 header values";
    const auto isInt = [] (const juce::String& s) { return s.isNotEmpty() && s.trimCharactersAtStart ("-").containsOnly ("0123456789"); };
    for (int i = 0; i < 7; ++i)
        if (i != 5 && ! isInt (values[i]))
            return "bad value on header line " + juce::String (i + 1) + " of the .kbm: '" + values[i] + "'";
    out = {};
    out.mapSize = values[0].getIntValue();
    out.firstNote = values[1].getIntValue();
    out.lastNote = values[2].getIntValue();
    out.middleNote = values[3].getIntValue();
    out.referenceNote = values[4].getIntValue();
    out.referenceHz = values[5].getDoubleValue();
    out.formalOctave = values[6].getIntValue();
    if (out.mapSize < 0 || out.mapSize > 1024 || out.referenceHz <= 0.0 || out.referenceNote < 0 || out.referenceNote > 127)
        return "invalid .kbm header (map size, reference note or frequency)";
    for (int i = 0; i < out.mapSize; ++i)
    {
        const auto v = 7 + i < values.size() ? values[7 + i] : juce::String ("x");
        if (v.equalsIgnoreCase ("x")) { out.map.push_back (-1); continue; }
        if (! isInt (v))
            return "bad mapping entry " + juce::String (i + 1) + ": '" + v + "'";
        out.map.push_back (v.getIntValue());
    }
    return {};
}

// Grado de la escala de una tecla (std::nullopt = sin mapear o fuera del rango retocado)
inline std::optional<int> degreeOf (const Scale& s, const Mapping& m, int key)
{
    const auto d = key - m.middleNote;
    if (m.mapSize == 0)
        return d;
    const auto rounds = (int) std::floor ((double) d / m.mapSize);
    const auto entry = m.map[(size_t) (d - rounds * m.mapSize)];
    if (entry < 0)
        return std::nullopt;
    const auto octave = m.formalOctave != 0 ? m.formalOctave : (int) s.cents.size();
    return entry + rounds * octave;
}

// Cents de un grado por extensión de periodo (el grado 0 es el 1/1)
inline double centsOf (const Scale& s, int degree)
{
    const auto n = (int) s.cents.size();
    const auto q = (int) std::floor ((double) degree / n);
    const auto r = degree - q * n;
    return q * s.cents.back() + (r == 0 ? 0.0 : s.cents[(size_t) r - 1]);
}

// Frecuencias de las 128 teclas (0 = sin mapear). Fuera de [primera, última] se queda en 12-TET
inline juce::String frequencies (const Scale& s, const Mapping& m, std::array<double, 128>& hz)
{
    const auto ref = degreeOf (s, m, m.referenceNote);
    if (! ref.has_value())
        return "the reference note of the .kbm is unmapped (x)";
    const auto refCents = centsOf (s, *ref);
    for (int k = 0; k < 128; ++k)
    {
        if (k < m.firstNote || k > m.lastNote)
        {
            hz[(size_t) k] = 440.0 * std::exp2 ((k - 69) / 12.0);
            continue;
        }
        const auto d = degreeOf (s, m, k);
        hz[(size_t) k] = d.has_value() ? m.referenceHz * std::exp2 ((centsOf (s, *d) - refCents) / 1200.0) : 0.0;
    }
    return {};
}

// Mapa por defecto sin .kbm (Scala SET MIDDLE / MAP_FREQ): grado 0 en la tecla 60 a 261,6256 Hz, lineal
inline Mapping defaultMapping()
{
    Mapping m;
    m.referenceNote = 60;
    m.referenceHz = 440.0 * std::exp2 (-9.0 / 12.0);
    return m;
}
} // namespace Scala

class Tuning
{
public:
    Tuning() { tables[0].fill (1.0); tables[1].fill (1.0); }
    ~Tuning() { if (client != nullptr) MTS_DeregisterClient (client); }

    // Hilo de mensajes. Carga desde texto (el estado guarda el texto: el preset no depende del fichero). kbm vacío = mapa
    // por defecto. Devuelve un error vacío si la afinación queda aplicada
    juce::String load (const juce::String& sclText, const juce::String& kbmText, const juce::String& name)
    {
        Scala::Scale scale;
        Scala::Mapping mapping = Scala::defaultMapping();
        if (auto e = Scala::parseScale (sclText, scale); e.isNotEmpty())
            return e;
        if (kbmText.isNotEmpty())
            if (auto e = Scala::parseMapping (kbmText, mapping); e.isNotEmpty())
                return e;
        std::array<double, 128> hz {};
        if (auto e = Scala::frequencies (scale, mapping, hz); e.isNotEmpty())
            return e;
        const auto next = 1 - active.load();
        auto& t = tables[(size_t) next];
        auto& f = unmapped[(size_t) next];
        f.reset();
        for (int k = 0; k < 128; ++k)
        {
            if (hz[(size_t) k] <= 0.0) { f.set ((size_t) k); t[(size_t) k] = 1.0; continue; }
            t[(size_t) k] = hz[(size_t) k] / (440.0 * std::exp2 ((k - 69) / 12.0));
        }
        active.store (next);
        scalaOn.store (true);
        scl = sclText; kbm = kbmText; scaleName = name.isNotEmpty() ? name : scale.description;
        return {};
    }

    void reset()
    {
        scalaOn.store (false);
        scl = kbm = scaleName = {};
    }

    bool hasScala() const { return scalaOn.load(); }
    juce::String getScl() const { return scl; }
    juce::String getKbm() const { return kbm; }
    juce::String getScaleName() const { return scaleName; }

    // MTS-ESP: el cliente se registra al activarlo la primera vez y solo se libera al destruir (el hilo de audio puede estar
    // usándolo: desactivar solo deja de consultarlo). Sin la librería LIBMTS instalada, el cliente nunca tiene maestro
    void setMtsEnabled (bool on)
    {
        if (on && client.load() == nullptr)
            client.store (MTS_RegisterClient());
        mtsEnabled.store (on);
    }
    bool isMtsEnabled() const { return mtsEnabled.load(); }
    // MTS-ESP manda en el último bloque: maestro conectado o SysEx MTS recibido (anunciador MTS del display)
    bool mtsActive() const { return mtsBlockActive.load (std::memory_order_relaxed); }
    bool mtsHasMaster() const { auto* c = client.load(); return mtsEnabled.load() && c != nullptr && MTS_HasMaster (c); }
    juce::String mtsScaleName() const { auto* c = client.load(); return c != nullptr ? juce::String (MTS_GetScaleName (c)) : juce::String(); }

    //==========================================================================
    // Hilo de audio

    // Al principio del bloque: decide la fuente del bloque (una consulta a MTS) y pasa el SysEx MTS al cliente
    void beginBlock (const juce::MidiBuffer& midi)
    {
        auto* c = mtsEnabled.load (std::memory_order_relaxed) ? client.load() : nullptr;
        blockClient = c;
        if (c != nullptr)
            for (const auto metadata : midi)
                if (metadata.numBytes > 3 && metadata.data[0] == 0xf0)
                    MTS_ParseMIDIDataU (c, metadata.data, metadata.numBytes);
        mtsBlockActive.store (c != nullptr && (MTS_HasMaster (c) || MTS_HasReceivedMTSSysEx (c)), std::memory_order_relaxed);
    }

    double noteRatio (int note) const noexcept
    {
        note = juce::jlimit (0, 127, note);
        if (mtsBlockActive.load (std::memory_order_relaxed) && blockClient != nullptr)
            return MTS_RetuningAsRatio (blockClient, (char) note, -1);
        if (scalaOn.load (std::memory_order_relaxed))
            return tables[(size_t) active.load (std::memory_order_acquire)][(size_t) note];
        return 1.0;
    }

    // Tecla que no debe sonar: 'x' del .kbm o filtrada por el maestro MTS-ESP
    bool isFiltered (int note) const noexcept
    {
        note = juce::jlimit (0, 127, note);
        if (mtsBlockActive.load (std::memory_order_relaxed) && blockClient != nullptr)
            return MTS_ShouldFilterNote (blockClient, (char) note, -1);
        return scalaOn.load (std::memory_order_relaxed) && unmapped[(size_t) active.load (std::memory_order_acquire)].test ((size_t) note);
    }

    // Quita del bloque los note-on de teclas filtradas y sus note-off (solo los de notas que se quitaron al pulsar: si la
    // afinación cambia con la tecla abajo, la nota que sonó se apaga igualmente)
    void filterNotes (juce::MidiBuffer& midi)
    {
        if (! mtsBlockActive.load (std::memory_order_relaxed) && ! scalaOn.load (std::memory_order_relaxed) && dropped.none())
            return;
        bool any = false;
        for (const auto metadata : midi)
        {
            const auto m = metadata.getMessage();
            if ((m.isNoteOn() && isFiltered (m.getNoteNumber())) || (m.isNoteOff() && dropped.test (index (m))))
            {
                any = true;
                break;
            }
        }
        if (! any)
            return;
        juce::MidiBuffer kept;
        for (const auto metadata : midi)
        {
            const auto m = metadata.getMessage();
            if (m.isNoteOn() && isFiltered (m.getNoteNumber())) { dropped.set (index (m)); continue; }
            if (m.isNoteOff() && dropped.test (index (m))) { dropped.reset (index (m)); continue; }
            kept.addEvent (m, metadata.samplePosition);
        }
        midi.swapWith (kept);
    }

private:
    static size_t index (const juce::MidiMessage& m) { return (size_t) (juce::jlimit (1, 16, m.getChannel()) - 1) * 128 + (size_t) m.getNoteNumber(); }

    std::array<std::array<double, 128>, 2> tables;
    std::array<std::bitset<128>, 2> unmapped;
    std::atomic<int> active { 0 };
    std::atomic<bool> scalaOn { false }, mtsBlockActive { false }, mtsEnabled { false };
    std::atomic<MTSClient*> client { nullptr };
    MTSClient* blockClient = nullptr;
    std::bitset<16 * 128> dropped;
    juce::String scl, kbm, scaleName;
};

namespace KoniTuning
{
inline thread_local const Tuning* current = nullptr;

// Factor sobre la frecuencia de 12-TET (A4 = 440 Hz) de la nota: 1.0 exacto sin afinación
inline double ratio (int note) noexcept { return current != nullptr ? current->noteRatio (note) : 1.0; }

// Fija la instancia actual mientras dura un processBlock
struct Scope
{
    explicit Scope (const Tuning& t) { current = &t; }
    ~Scope() { current = nullptr; }
};
} // namespace KoniTuning
