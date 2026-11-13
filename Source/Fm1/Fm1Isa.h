// Decodificador de instrucciones pi32v2, la CPU del JieLi AC791N del M-VAVE FM-1.
//
// Port a C++ de fm1-emu (Joakim Eriksson, licencia MIT, https://github.com/joakimeriksson/fm1-emu), crate fm1-isa:
// misma tabla (Fm1IsaTable.inc, generada por tools/fm1/gen_isa_cpp.py) y mismo algoritmo de decodificación.
// Cabecera independiente (sin JUCE): la usan el motor FM-1 y las pruebas (Tests/Fm1Tests.cpp).
//
// Numeración de bits: los bytes de la instrucción b0..bn forman un entero little-endian (b0 = bits 0..7...). Se
// decodifica sobre una ventana de hasta 6 bytes; la tabla se recorre en orden de prioridad (más larga, más bits fijos
// en la máscara, más frecuente en el corpus) y una entrada cuyas muestras del corpus contienen los bytes observados
// gana el empate a sus hermanas.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fm1
{
// Tipos de ranura de operando (resueltos por el solver de fm1-emu sobre todo el corpus)
enum class SlotKind : uint8_t
{
    Imm,       // concatenación little-endian de las piezas = valor impreso
    Rel,       // mult · sext(concat): saltos, en bytes desde el pc siguiente
    Adj,       // concat - adj (adj positivo: p. ej. N-1 codificado)
    Blk,       // bloque de registros: de max(n, hi) a min(n, lo), descendente
    Scaled,    // mult · concat - adj (desplazamientos sin signo escalados; rep N = 2·(f+1))
    Const,     // constante (adj la guarda); no consume bits
    Zero32,    // cuentas de desplazamiento: un campo 0 vale 32
    Bit,       // 1 << concat (máscaras de poner/probar bit)
    NBit,      // ~(1 << concat) (máscaras de borrar bit)
    Composed,  // inmediato compuesto de 12 bits en los bits 16-27 (composedImm)
    NComposed  // el compuesto invertido (formas &= ~x)
};

struct Fm1SlotSpec
{
    uint8_t kind;          // SlotKind
    uint8_t numPieces;
    uint8_t pieces[6][2];  // tramos de bits contiguos (lsb, msb), concatenados empezando por el bajo
    int32_t mult, adj;
    uint8_t hi, lo;
};

// Una clase de instrucción (una entrada del YAML de fm1-emu)
struct Fm1IsaEntry
{
    const char* name;
    const char* syntax;    // sintaxis normalizada principal, p. ej. "[--sp] = {rets, R*2}"; " #" final = cabeza de par
    const char* canon;     // la de canon_syntax de fm1-core: sin el signo de "-#i" ni el " #" final (clave del ejecutor)
    uint8_t len;           // longitud en bytes
    uint32_t count;        // apariciones en el corpus (peso para informes)
    uint64_t mask, match;  // bits fijos y su valor sobre la ventana little-endian
    const char* const* alts;   uint16_t altCount;     // otras grafías de la misma familia
    const uint64_t* samples;   uint32_t sampleCount;  // muestras del corpus (desempate entre máscaras hermanas)
    const Fm1SlotSpec* slots;  uint8_t slotCount;     // ranuras de operando verificadas
};

// El inmediato compuesto de 12 bits de "R = #h", las formas ALU registro-inmediato de 4 bytes y "[R+#i] op= #h":
// código de 4 bits y m de 8 bits. Ajustado por fm1-emu sobre todo ALU/mov inmediato de 4 bytes de V13 (6878 + 129
// muestras, 0 discrepancias):
//   código 0      m literal
//   código 1      m repetido por media palabra        (0x5f -> 0x005f005f)
//   código 2      m << 8 repetido por media palabra   (0x64 -> 0x64006400)
//   código 3      m repetido por byte                 (0xcc -> 0xcccccccc)
//   código 4..15  tipo coma flotante: exponente E = código << 1 | m >> 7 (8..31), mantisa 0x80 | (m & 0x7f),
//                 valor = mantisa << (32 - E)         (0x4,0x7f -> 0xff000000; 0xf,0xff -> 0x1fe)
uint32_t composedImm (uint32_t code, uint32_t m) noexcept;

const Fm1SlotSpec& slotOf (const Fm1IsaEntry& e, int i) noexcept;
uint32_t slotConcat (const Fm1SlotSpec& s, uint64_t raw) noexcept;  // concatenación little-endian de las piezas
int slotBits (const Fm1SlotSpec& s) noexcept;
int64_t slotValue (const Fm1SlotSpec& s, uint64_t raw) noexcept;    // registros: su número; bloques: el campo n
// Registros de un bloque, de mayor a menor: escribe hasta 32 en out y devuelve cuántos
int slotBlockRegs (const Fm1SlotSpec& s, uint64_t raw, uint8_t out[32]) noexcept;

const char* altOf (const Fm1IsaEntry& e, int i) noexcept;
bool hasSyntax (const Fm1IsaEntry& e, const char* form) noexcept;  // la principal o cualquier alias

// Decodifica la ventana little-endian (ceros por encima de lo leído) de hasta 6 bytes; nullptr si ninguna clase
// coincide (UnknownInstruction en fm1-emu). La FPU va primero: sus codificaciones no salen en el corpus del objdump y
// las comparaciones en coma flotante son las enteras con un bit de más que las máscaras del corpus no fijan.
const Fm1IsaEntry* decodeWin (uint64_t win) noexcept;
const Fm1IsaEntry* decodeBytes (const uint8_t* bytes, size_t n) noexcept;

size_t entryCount() noexcept;                         // clases de la tabla minada (sin FPU ni respaldo)
const Fm1IsaEntry& entryAt (size_t i) noexcept;       // la clase i de esa tabla (i < entryCount())
const Fm1IsaEntry* findEntry (const char* name) noexcept;  // por nombre (pruebas y diagnóstico)
} // namespace fm1
