// JIT del núcleo pi32v2 (J2, diseño en User/FM1/JIT-DESIGN.md): búfer de código ejecutable con W^X y un emisor x86-64
// mínimo. Solo Windows x64 y Linux x86-64; en el resto (macOS, ARM) jitSupported() es false y se queda el intérprete.
// Cabecera independiente (sin JUCE ni windows.h): la usan el motor FM-1, KoniFm1Run y KoniFm1Tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace fm1
{
constexpr bool jitSupported() noexcept
{
#if (defined(_WIN32) && defined(_M_X64)) || (defined(__linux__) && defined(__x86_64__))
    return true;
#else
    return false;
#endif
}

// Calidad de servicio alta para el hilo que llama (el del emulador): en Windows 10 1709 o posterior quita su limitación
// de energía (EcoQoS), con lo que el planificador lo lleva a los núcleos de rendimiento de una CPU híbrida (medido en un
// Core Ultra 9 288V con GHOULBOX y 12 notas: P 1,26-1,42x, E 1,03-1,04x) sin fijarlo a ninguno. En el resto de
// plataformas no hace nada. false si el sistema no lo admite
bool preferPerformanceCores() noexcept;

// Memoria para código nativo: se reserva de una vez, nunca escribible y ejecutable a la vez (W^X). Preferida: la misma
// memoria proyectada dos veces, una vista de escritura y otra de ejecución, sin llamadas al sistema por bloque. Si el
// sistema no la da: una sola vista, y cada bloque pasa sus páginas a escritura, se copia y vuelve a ejecución (dos
// VirtualProtect / mprotect por bloque: medido, la mitad del tiempo de compilar)
class CodeBuffer
{
public:
    CodeBuffer() = default;
    CodeBuffer (const CodeBuffer&) = delete;
    CodeBuffer& operator= (const CodeBuffer&) = delete;
    ~CodeBuffer();

    bool init (size_t bytes);  // false: sin memoria ejecutable (o plataforma sin JIT)
    // Copia el código y devuelve dónde quedó (ejecutable), o nullptr si no cabe
    void* add (const uint8_t* code, size_t n);
    size_t used() const noexcept { return top; }
    void reset() noexcept { top = 0; }  // (todo el código anterior deja de usarse: el JIT vacía sus índices antes)

private:
    uint8_t* base = nullptr;   // la vista que se ejecuta
    uint8_t* wbase = nullptr;  // la de escritura (doble vista); nullptr: una sola vista con cambios de protección
    void* mapping = nullptr;   // (Windows) la sección de las dos vistas
    int fd = -1;               // (Linux) el memfd de las dos vistas
    size_t size = 0, top = 0;
};

// Emisor: los bytes de las pocas formas que usa el JIT (codificación x86-64, Intel SDM vol. 2)
struct Emitter
{
    std::vector<uint8_t> b;
    void u8 (uint8_t v) { b.push_back (v); }
    void u32 (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); }
    void u64 (uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back ((uint8_t) (v >> (8 * i))); }
    void bytes (std::initializer_list<uint8_t> l) { b.insert (b.end(), l); }
    size_t pos() const noexcept { return b.size(); }
    void patch32 (size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + (size_t) i] = (uint8_t) (v >> (8 * i)); }
};
} // namespace fm1
