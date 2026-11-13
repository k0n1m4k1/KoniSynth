// Núcleo pi32v2 (JieLi AC791N, M-VAVE FM-1): intérprete de referencia.
//
// Port a C++ de fm1-emu (Joakim Eriksson, licencia MIT, https://github.com/joakimeriksson/fm1-emu, commit 3e3fb27),
// crate fm1-core: el estado de la CPU, decode_any (la tabla más las familias que se decodifican por sus campos),
// execute / exec_syntax / exec_raw / finish, los pares de emisión doble, los bucles rep, los bloques predicados
// if {} else {} y las interrupciones. Es la ruta lenta de fm1-emu, la de referencia: sin micro-ops (Fast), sin bloques
// y sin JIT. fm1-emu ejecuta con micro-ops las instrucciones que las tienen y sus pruebas comprueban que dan lo mismo
// que exec_syntax y que exec_raw; la ruta rápida de KoniSynth se contrastará contra esta.
//
// Las afirmaciones del original que este marca como supuestas o pendientes de probar en el aparato se conservan como
// comentarios con "supuesto (fm1-emu)".
#pragma once

#include "Fm1Isa.h"

#include <cstdint>
#include <vector>

namespace fm1
{
// El bus que ve un núcleo (memoria y periféricos). Little-endian; las lecturas no alineadas las resuelve el bus.
struct Bus
{
    virtual ~Bus() = default;
    virtual uint8_t read8 (uint32_t addr) = 0;
    virtual uint16_t read16 (uint32_t addr) = 0;
    virtual uint32_t read32 (uint32_t addr) = 0;
    virtual uint32_t fetch32 (uint32_t addr) { return read32 (addr); }  // lectura de instrucciones (sin efectos)
    virtual void write8 (uint32_t addr, uint8_t v) = 0;
    virtual void write16 (uint32_t addr, uint16_t v) = 0;
    virtual void write32 (uint32_t addr, uint32_t v) = 0;
    // lockset / lockclr: el cerrojo hardware que comparten los dos núcleos; false = lo tiene el otro (se reintenta)
    virtual bool hwLock (uint32_t /*core*/, bool /*set*/) { return true; }
    virtual void spin (uint32_t /*core*/, uint32_t /*addr*/) {}  // el núcleo espera en un cerrojo de byte (testset)
    virtual void idle (uint32_t /*core*/) {}                      // "idle": el núcleo para hasta una interrupción
};

enum class Fault : uint8_t
{
    None,
    Decode,       // ninguna clase coincide con los bytes (UnknownInstruction)
    Unsupported,  // la clase no tiene semántica (o le falta una ranura de operando)
    PredOverflow  // más de kMaxPred bloques predicados anidados (fm1-emu no tiene límite)
};

// Bits de las banderas aritméticas en los bits bajos de psr, como los lee el código compilado: slt mira N ^ V,
// ult mira C (1 = sin préstamo, como en ARM). Z: supuesto (fm1-emu).
constexpr uint32_t kFlagN = 1, kFlagC = 2, kFlagZ = 4, kFlagV = 8;

// Información de una instrucción que no depende del estado de la máquina: se calcula una vez al decodificar
struct Fm1Pre
{
    enum RhsKind : uint8_t { RhsReg, RhsImm, RhsBit };  // Bit: 1 << r[n]
    struct Rhs { RhsKind kind; uint32_t v; };
    enum AluOp : uint8_t { Add, Sub, RSub, Or, Xor, And, AndNot, Mul, Shl, Shr, Sar };

    uint16_t route = 0;
    bool pairHead = false;  // instrucción impresa con " #": se ejecuta junto con la siguiente
    bool hasAlu4 = false, hasBitTest = false;
    // ALU de 4 bytes (decode_alu4): destino registro o memoria [src + memOff]
    bool aluDstMem = false;
    uint8_t aluDst = 0, aluSrc = 0;
    AluOp aluOp = Add;
    Rhs aluRhs {};
    int32_t aluMemOff = 0;
    // salto por prueba de bits (decode_bittest)
    uint8_t btReg = 0;
    Rhs btMask {};
    bool btNe = false;
    int64_t btOff = 0;
};

class Cpu
{
public:
    explicit Cpu (uint32_t entry = 0);

    // Ejecuta una instrucción. false = fallo (fault, faultName, faultPc, faultRaw); el pc no avanza.
    bool step (Bus& bus);

    // ¿Se atendería ahora una interrupción de prioridad prio? Solo entre construcciones completas: no dentro de un
    // bloque rep, de un if {} predicado ni de un par de emisión doble, cuyo estado vive en el núcleo y no en la pila.
    bool irqReady (uint8_t prio) const noexcept;
    // Entra en el manejador: reti = pc interrumpido (el envoltorio de la ISR guarda {psr, rets, reti} y vuelve con rti)
    void interrupt (uint32_t handler, uint8_t prio);

    uint32_t icfg() const noexcept;
    uint32_t sreg (const char* name) const noexcept;

    // ---- estado arquitectónico (público, como en fm1-emu: el SoC y las pruebas lo leen y lo escriben) ----
    uint32_t coreId = 0;  // lo que devuelve cnum (el AC791N tiene dos núcleos)
    uint32_t pc = 0;
    uint32_t regs[16] {};
    uint32_t sp = 0, ssp = 0, usp = 0;
    // Modo usuario: sp es la pila en uso; usp y ssp son registros propios. Una interrupción en modo usuario guarda sp
    // en usp y sigue en ssp; el rti que vuelve a usuario guarda sp en ssp y sigue con usp. En supervisor la ISR se
    // queda en su pila.
    bool user = false;
    uint32_t userIrq = 0;  // por profundidad de interrupción: ¿el código interrumpido estaba en usuario? (bit = nivel)
    uint32_t rets = 0;
    uint64_t insnCount = 0;
    // Bloque rep: los N bytes tras la instrucción rep; al salir por su final se descuenta y, si queda, vuelve al
    // principio. repEnd == 0: no hay rep activo. El contador se fija al ejecutar rep (forma registro: el registro
    // queda a 0).
    uint32_t repStart = 0, repEnd = 0, repCount = 0;
    // Bloques if {} else {} predicados con la parte then en curso: (thenEnd, elseEnd, thenStart)
    struct PredSkip { uint32_t thenEnd, elseEnd, thenStart; };
    std::vector<PredSkip> predSkips;
    bool cc = false;           // bandera de testset
    uint32_t flags = 0;        // N C Z V (psr & 0xf)
    uint32_t sregStore[7] {};  // icfg, reti, retx, rete, sspn, psr, sr4
    // Par de emisión doble en curso: la cabeza (" #") se ejecuta y sus resultados de registro se guardan aquí; la
    // segunda instrucción aún ve los valores viejos; después se confirman los que la segunda no escribió.
    bool pairPending = false;
    uint32_t pairOld[16] {}, pairNew[16] {};
    bool ie = false;  // habilitación global (sti / cli); el bit 8 de icfg es la segunda llave
    std::vector<uint8_t> irqLevels;  // prioridades de las ISR en curso, la más interna al final
    // El anidamiento solo se abre mientras la ISR tiene reti guardado en la pila (regla de Blackfin)
    bool nestOk = false;
    uint32_t holdIrq = 0;

    Fault fault = Fault::None;
    const char* faultName = nullptr;
    uint32_t faultPc = 0;
    uint64_t faultRaw = 0;
    uint64_t decodes = 0;  // fallos de la caché de instrucciones desde el reset

    static constexpr size_t kMaxPred = 64;

private:
    struct Line
    {
        uint32_t pc = 0xffffffffu;
        uint8_t len = 0;
        uint64_t win = 0;   // los bytes de la instrucción (para validar líneas fuera de la XIP)
        uint64_t raw = 0;   // el raw que ve la ejecución (la ventana o sus 4 bytes bajos, según decode_any)
        const Fm1IsaEntry* entry = nullptr;
        Fm1Pre pre;
    };
    std::vector<Line> icache;

    Line* fetch (Bus& bus, uint32_t at);
    bool execute (Bus& bus, const Line& l, uint32_t nextPc);
    // Devuelve false si la forma no tiene semántica; jump/hasJump = salto tomado
    bool execSyntax (Bus& bus, const Line& l, uint32_t nextPc, bool& hasJump, uint32_t& jump);
    void execRaw (Bus& bus, uint8_t route, uint32_t raw);
    void finish (bool hasJump, uint32_t jump, uint32_t nextPc);
    uint32_t blockEnd (Bus& bus, uint32_t pc, uint32_t units) const;

    // Ranuras de operando (las de exec_syntax): si falta una, missingSlot y 0
    int64_t slot (const Line& l, int i);
    uint32_t v (const Line& l, int i) { return (uint32_t) slot (l, i); }
    int r (const Line& l, int i) { return (int) (slot (l, i) & 0xf); }
    bool missingSlot = false;

    uint32_t sregNamed (const char* n, size_t len) const noexcept;
    void setSregNamed (const char* n, size_t len, uint32_t v) noexcept;
    void setSreg (const char* n, size_t len, uint32_t v) noexcept;
    void setIcfg (uint32_t v) noexcept { sregStore[0] = v; ie = (v & 0x200) != 0; }
    uint32_t aluF (Fm1Pre::AluOp op, uint32_t a, uint32_t b) noexcept;
    bool fail (Fault f, const Line* l);
};

// Decodifica la instrucción en pc desde su ventana de 8 bytes: la tabla y, antes o después, las familias que se
// decodifican por sus campos (las clases de la tabla solo encajan con las muestras del corpus). raw = lo que ve la
// ejecución. nullptr si nada coincide.
const Fm1IsaEntry* decodeAny (uint64_t win, uint64_t& raw) noexcept;

// Ruta de execute (route_of de fm1-core), con alu4 y bittest ya resueltos en pre
void computePre (const Fm1IsaEntry& e, uint64_t raw, Fm1Pre& pre) noexcept;
} // namespace fm1
