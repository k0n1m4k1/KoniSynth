// Núcleo pi32v2 (JieLi AC791N, M-VAVE FM-1): intérprete de referencia.
//
// Port a C++ de fm1-emu (Joakim Eriksson, licencia MIT, https://github.com/joakimeriksson/fm1-emu, commit 3e3fb27),
// crate fm1-core: el estado de la CPU, decode_any (la tabla más las familias que se decodifican por sus campos),
// execute / exec_syntax / exec_raw / finish, los pares de emisión doble, los bucles rep, los bloques predicados
// if {} else {} y las interrupciones. Además, micro-ops (Fm1Pre::fast) para las formas comunes de exec_syntax; sin
// bloques ni JIT. Con fastPath = false todo va por exec_syntax (la referencia): KoniFm1Run --reference 1 compara el
// hash del estado final de las dos rutas.
//
// Las afirmaciones del original que este marca como supuestas o pendientes de probar en el aparato se conservan como
// comentarios con "supuesto (fm1-emu)".
#pragma once

#include "Fm1Isa.h"
#include "Fm1Jit.h"

#include <deque>
#include <memory>

#include <cstdint>
#include <unordered_map>
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
    // (JIT) la RAM de datos para accesos en línea: sus bytes, dirección y tamaño, la marca de código por palabra y el
    // puntero de apuntado de escrituras (Ram::log). false: sin acceso en línea (todo por el bus)
    struct RamView { uint8_t* data; uint32_t base, size; const uint8_t* code; void* const* log; const uint64_t* codeWrites; };
    virtual bool ramView (RamView&) { return false; }
    // la XIP (flash mapeada), para cargas en línea en el JIT: se lee como Soc::read32 (su contenido puede cambiar al
    // escribir la flash; el búfer no se mueve ni cambia de tamaño)
    struct XipView { const uint8_t* data; uint32_t base, size; };
    virtual bool xipView (XipView&) { return false; }
    // (JIT) marca como código las palabras de RAM de [lo, hi) y cuenta las escrituras a palabras marcadas: si cambia,
    // los bloques hechos desde RAM ya no valen
    virtual void markCode (uint32_t /*lo*/, uint32_t /*hi*/) {}
    virtual uint64_t codeWrites() { return 0; }
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
    // Micro-operación (ruta rápida, como fast_of de fm1-emu) para las formas comunes que van por exec_syntax: operandos
    // resueltos al decodificar, la misma semántica. FastNone = sin micro-op (va por exec_syntax)
    enum FastOp : uint8_t { FastNone, FastLd, FastSt, FastBrI, FastBrR, FastAluI, FastAluR, FastMov, FastMovI, FastGoto, FastCall,
                            FastRts, FastPart,
                            // segunda tanda (la cola de exec_syntax medida con la V15)
                            FastLdX,    // carga con cualquier direccionamiento (fmode), incrementos previos y posteriores
                            FastStX,    // almacenamiento o lectura-modificación-escritura (fcond = op, fsrc = fuente)
                            FastSpAdd,  // sp += #i
                            FastSpPlus, // R = sp + #i
                            FastCnum,   // R = cnum (el núcleo: no se resuelve al decodificar)
                            FastField,  // uextra / sextra / insert (fcond = 0 / 1 / 2; fsh = posición, fw = longitud)
                            FastAdc,    // R = R + R + c
                            FastIe,     // sti / cli (fimm = 1 / 0)
                            FastAndNot, // R = R & ~R
                            FastShift,  // R = R <<|>>|>>>|<<< #i|R (fcond = 0..3; fidx: la cuenta en un registro)
                            FastDecBr,  // if (--R <op> #i) goto #i
                            FastPopPc,  // pc = [sp++]
                            FastIcfgRd, // R = icfg (prioridades en curso y habilitación global: Cpu::icfg)
                            FastIcfgWr, // icfg = R (Cpu::setIcfg)
                            // tercera tanda: la cola de exec_syntax medida con GHOULBOX 2.4 y la V15 (2026-11-14)
                            FastDiv,    // R = R / R | R % R (fcond: 0 / (u), 1 / (s), 2 % (u), 3 % (s); entre 0 = 0)
                            FastAbs,    // R = abs(R) (wrapping)
                            FastNot,    // R = ~R
                            FastTb,     // tbb [R] / tbh [R] (fw = 1 / 2): pc = siguiente + 2·tabla[R]
                            FastCallR,  // call R
                            FastRti,    // rti (Cpu::doRti)
                            FastPushList, // [--sp] = {R*N}: fimm = máscara de registros, del mayor al menor
                            FastPopList,  // {R*N} = [sp++]: fimm = máscara, del menor al mayor
                            // cuarta tanda (cobertura del JIT): rutas crudas 26, 28 y 0
                            FastPushRets, // [--sp] = {rets, R*N}: rets primero, luego la máscara fimm del mayor al menor
                            FastPopRetPc, // {pc, R*N} = [sp++]: la máscara fimm del menor al mayor, luego pc
                            FastBitBr,    // if ((R & rhs) != 0) == fcond goto: fmode = clase de rhs (Fm1Pre::RhsKind),
                                          // fb = su registro, fimm = su inmediato
                            // quinta tanda (cobertura del JIT, medida con el osciloscopio de la V15): ruta cruda 2
                            FastNop,      // nop / csync / sti R / cli R: sin efecto (como su caso en execRoute)
                            // sexta tanda: las rutas crudas de datos (16, 18, 19, 21, 22, 32..41), sin saltos ni estado de
                            // interrupciones: el mismo Cpu::execRaw que execRoute
                            FastExecRaw,
                            // séptima tanda (Mutable Dreams: 3 M bloques cortados en 4 s con 12 notas)
                            FastMovPair,  // rH_rL = #i: regs[fd] = fimm (bajo), regs[fb] = foff (alto)
                            FastBlock     // [R+] = {..} / {..} = [R+] y R++, -R, --R (eb): fa = base, fimm = máscara,
                                          // fcond = store | wb << 1 | down << 2
    };
    // Direccionamientos de FastLdX / FastStX (los de exec_syntax)
    enum MemMode : uint8_t { MemRImm, MemSpImm, MemSp, MemRR, MemRRShl, MemPostImm, MemPostR, MemPreImm, MemPreR, MemPushSp };
    enum MemSrc : uint8_t { SrcReg, SrcImm, SrcRets };
    // (OpShl / OpShr / OpSar: la ALU de la ruta 1 con destino en memoria y cuenta inmediata, & 31 como alu())
    // (OpAndNot: [R+#i] &= ~R, de exec_syntax)
    enum MemOp : uint8_t { OpSet, OpOr, OpAnd, OpXor, OpAdd, OpSub, OpShl, OpShr, OpSar, OpAndNot };
    uint8_t fmode = 0, fsrc = 0;
    FastOp fast = FastNone;
    uint8_t fd = 0, fa = 0, fb = 0;  // destino, fuente / base, segundo registro (índice o rhs)
    uint8_t fw = 4, fsh = 0;         // ancho de acceso; desplazamiento del índice (R+R<<#i) o parte (FastPart)
    uint8_t fcond = 0;               // condición (FastBr*) u operación (FastAlu*: AluOp)
    bool fsign = false, fsp = false, fidx = false, fflags = false;
    uint32_t fimm = 0;
    int64_t foff = 0;                // desplazamiento del salto desde el pc siguiente
    // ruta 13 (comparación de registros con salto) y 14 (if {} predicado), decodificadas una vez: familia de comparación,
    // invertida, registros (13: a, b; 14: reg y rhs), desplazamiento (13) y unidades then / else (14)
    uint8_t cbFam = 0, cbA = 0, cbB = 0, ibThen = 0, ibElse = 0;
    // lo que unas rutas crudas leían del nombre en cada ejecución: 3 lockset, 9 "and", 10 1 lsr / 2 asr, 25 "ne",
    // 29 rep_i_r (1 = sí)
    uint8_t nameKind = 0;
    bool cbAlt = false;
    int64_t cbOff = 0;
    Rhs ibRhs {};
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

    // Ráfaga de Machine::runBlocks: instrucciones seguidas sin las comprobaciones de su ruta de arriba mientras no puedan
    // cambiar su resultado (las condiciones, aquí; las razones, en runBlocks). Antes de cada instrucción: un acceso con
    // efectos (sideEffects), el tramo (room), el próximo evento (cycles + 1 >= due), un pc donde la ruta de arriba
    // actuaría (HLE, bucle contado, sondeo de bucles: Machine::burstStop) o la interrupción bloqueada que ya entraría.
    // Paga el tick de cada instrucción (++cycles) y deja inIsr al día
    struct BurstStop
    {
        const uint64_t* sideEffects = nullptr;
        uint64_t side0 = 0;
        uint64_t* cycles = nullptr;
        uint64_t due = 0;
        bool* inIsr = nullptr;
        uint32_t room = 0;
        int blockedPrio = -1;
        uint32_t stopA = 0xffffffffu, stopB = 0xffffffffu;
        bool loopEnabled = false;
        uint64_t nextTry = 0, backoffUntil = 0;
        const uint32_t* loops = nullptr;  // 64 pc (mapeo directo por (pc >> 1) & 63)
        uint32_t probeKnown = 0, probeMin = 0;
        // (precalculado) el sondeo en cualquier pc: now >= max (nextTry, backoffUntil) con room >= probeMin; y si la tabla
        // de bucles tiene alguno (si no, no se mira por pc)
        uint64_t probeAnyAt = 0;
        bool anyLoops = true;
        uint64_t loopsMask = ~0ull;  // entradas ocupadas de loops (bit = (pc >> 1) & 63; el JIT encadenado lo mira)
    };
    // Devuelve las instrucciones hechas; ok = false si la última falló (no cuenta); trip = la última salió de las
    // ventanas de pc (la excepción la lanza Machine)
    uint32_t burst (Bus& bus, const BurstStop& s, bool& ok, bool& trip);

    // ¿Se atendería ahora una interrupción de prioridad prio? Solo entre construcciones completas: no dentro de un
    // bloque rep, de un if {} predicado ni de un par de emisión doble, cuyo estado vive en el núcleo y no en la pila.
    bool irqReady (uint8_t prio) const noexcept;
    // Entra en el manejador: reti = pc interrumpido (el envoltorio de la ISR guarda {psr, rets, reti} y vuelve con rti)
    void interrupt (uint32_t handler, uint8_t prio);

    uint32_t icfg() const noexcept;
    uint32_t sreg (const char* name) const noexcept;

    // Lo que las instrucciones ven del núcleo entre instrucciones, fuera de pares, bloques predicados, bucles rep e
    // interrupciones (plain_state de fm1-core): el reset del chip vuelve a uno tomado al arrancar
    struct PlainState
    {
        uint32_t pc, regs[16], sp, ssp, usp;
        bool user;
        uint32_t userIrq, rets, repStart, repCount;
        bool cc;
        uint32_t flags, sregStore[7];
        bool ie, nestOk;
        bool operator== (const PlainState&) const = default;
    };
    bool plainState (PlainState& out) const noexcept;  // false si no está en un estado simple

    // Bucle de retardo contado en pc: un cuerpo de nop (ruta 2) con una sola suma "R += #d" y el cierre
    // "if (R != #C) goto pc". Salta vueltas enteras (las que caben en room y dejan al menos una por hacer): R y las
    // banderas quedan como tras la última suma saltada. room = instrucciones que pueden pasar sin evento. Devuelve las
    // instrucciones saltadas; 0 si ahora no hay nada que saltar; -1 si el código en pc no tiene esa forma
    int64_t skipCountedLoop (Bus& bus, uint32_t room);
    uint32_t loopHint = 0xffffffffu;  // destino del último "if (R != #C) goto" hacia atrás tomado (dónde mirar)
    // pc que no tenían la forma de un bucle contado (no volver a mirarlos): 8, directa por (pc >> 1) & 7. Con uno solo,
    // dos bucles anidados se pisaban y cada vuelta paraba la ráfaga y la cadena del JIT (HYP-1: el 57 % de sus salidas)
    uint32_t notCounted[8] { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
                             0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
    bool counted (uint32_t at) const noexcept { return notCounted[(at >> 1) & 7] != at; }
    void markNotCounted (uint32_t at) noexcept { notCounted[(at >> 1) & 7] = at; }
    void setPlainState (const PlainState& s) noexcept;

    // Límites de PC (las guardas de la CPU): el código corre dentro de alguna de las dos ventanas; una instrucción
    // traída fuera de ambas dispara la excepción. Devuelve si esta entrada es la que sale (trip)
    bool pcEnter (uint32_t at) noexcept  // (en línea: se mira antes de cada instrucción con ventanas de pc)
    {
        const bool outside = hasPcWin && ! (pcWin[0][0] <= at && at <= pcWin[0][1]) && ! (pcWin[1][0] <= at && at <= pcWin[1][1]);
        const bool trip = outside && ! pcOut;
        pcOut = outside;
        return trip;
    }

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
    uint32_t pairBefore[16] {};  // los registros antes de la cabeza (aquí y no en la pila de step: sin cookie /GS por instrucción)
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
    bool fastPath = true;  // micro-ops para las formas comunes (false = solo exec_syntax: la referencia)
    // JIT (User/FM1/JIT-DESIGN.md). En la ráfaga, un bloque de la XIP (instrucciones con micro-op, sin pares ni bloques
    // if {} / rep, hasta el primer cambio de flujo o del estado de interrupciones) corre como código nativo; las paradas de
    // la ráfaga se miran una vez para todo su rango. Fase J2.0: cada instrucción es una llamada a jitOp (execFast), la
    // misma semántica. Solo con la ruta rápida, sin perfiles y en las plataformas con JIT (jitSupported)
    bool jitOn = true;
    // (KoniFm1Run --jitprofile) instrucciones que el JIT hace con jitOp, por clave fast << 24 | ruta << 16 | fmode << 8 |
    // fidx << 7 | fcond (en las crudas, en lugar de fmode, la subclase: ver jitOp); nullptr: sin contar
    std::unordered_map<uint32_t, uint64_t>* jitOpHist = nullptr;
    // (KoniFm1Run --stepprofile) instrucciones que la ráfaga interpreta, por motivo << 28 | pc (1: estado de la CPU, 2: sin
    // bloque del JIT, 3: el bloque no cabe en lo que queda); nullptr: sin contar
    std::unordered_map<uint32_t, uint64_t>* stepHist = nullptr;
    // salidas de la cadena del JIT a la ráfaga: [JitCtx::why] con status 0; [8 + status] con 1 (acceso con efectos) o 2
    uint64_t jitExits[64] {};
    uint64_t jitCompileNs = 0;  // tiempo real gastado en compilar bloques (jitAt)
    uint64_t jitRuns = 0, jitInsns = 0, jitCompiled = 0;  // bloques ejecutados, sus instrucciones, bloques compilados
    uint64_t jitRamFlushes = 0, jitResets = 0, jitLinked = 0;  // (y enlaces hechos entre bloques)  // vaciados de los bloques de RAM (código reescrito) y del búfer entero
    // Un bloque del JIT en pc fuera de la ráfaga (el bucle de cpu1, Machine::runCpu1): como mucho maxN instrucciones, sin
    // ninguna de avoidA / avoidB ni del rango [avoidLo, avoidHi] después de la primera (ganchos del HLE, pc de sondeo) y
    // parando tras un acceso que cambie *side. Devuelve las instrucciones hechas; 0 = ningún bloque (y ok = false si
    // falló la primera). ok = false: falló una (fault ...)
    uint32_t runJit (Bus& bus, uint32_t maxN, uint32_t avoidA, uint32_t avoidB, uint32_t avoidLo, uint32_t avoidHi,
                     const uint64_t* side, bool& ok);
    // Perfil opcional (nullptr = apagado): instrucciones ejecutadas por clase
    std::unordered_map<const Fm1IsaEntry*, uint64_t>* hist = nullptr;
    // con hist: instrucciones por ruta (ruta rápida: el índice 255) y cabezas de par de emisión doble
    uint64_t routeCount[256] {}, pairHeads = 0;
    std::unordered_map<const Fm1IsaEntry*, uint64_t>* synHist = nullptr;  // las que van de verdad por exec_syntax
    std::unordered_map<uint32_t, uint64_t>* pcHist = nullptr;  // instrucciones ejecutadas por pc (también las de los sondeos)
    bool hasPcWin = false;
    uint32_t pcWin[2][2] {};  // (bajo, alto) de cada ventana
    bool pcOut = false;

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
        // if {} predicado en la XIP (no cambia): los finales de sus partes, calculados la primera vez que se ejecuta
        mutable bool ifKnown = false;
        mutable uint32_t ifThenEnd = 0, ifElseEnd = 0;
    };
    std::vector<Line> icache;

    // ---- JIT ----
    struct JitCtx
    {
        Cpu* cpu;
        Bus* bus;
        uint64_t now;               // ciclo de la primera instrucción del bloque
        uint64_t* cycles;
        const uint64_t* sideEffects;
        uint64_t side0;
        uint32_t done = 0;          // instrucciones acabadas (todas las de la cadena)
        uint32_t status = 0;        // 0 bloque entero, 1 parada tras un acceso con efectos, 2 fallo
        // encadenado (la segunda entrada de cada bloque lo mira antes de seguir; si algo no vale, vuelve)
        uint32_t base = 0;          // instrucciones de la cadena antes del bloque en curso
        uint32_t budget = 0;        // cuántas puede hacer la cadena (tramo, siguiente evento, hueco de sondeo)
        uint32_t stopA = 0xffffffffu, stopB = 0xffffffffu;  // pc donde la ráfaga pararía
        uint32_t avoidLo = 1, avoidHi = 0;                  // rango que ningún bloque encadenado puede tocar
        uint32_t win[4] = { 0, 0xffffffffu, 0, 0xffffffffu };  // ventanas de pc (Cpu::pcWin): el bloque, dentro de una
        uint64_t loopsMask = 0;     // entradas ocupadas de la tabla de bucles (con loopEnabled)
        uint64_t ramCw = 0;         // Ram::codeWrites cuando se miraron los bloques de RAM
        void* lastLink = nullptr;   // el último enlace por el que salió un bloque
        uint8_t loopEnabled = 0;
        // por qué volvió la cadena a la ráfaga (Cpu::jitExits): en la entrada encadenada, 1 presupuesto, 11 ventanas de pc,
        // 12 stopA/B, 13 avoid, 14 loopHint, 15 tabla de bucles, 7 código en RAM reescrito; 2 enlace sin
        // bloque o sin enlazar, 3 destino calculado fuera de la itab, 6 if con else sin cumplirse, 16 + fast el final de un
        // bloque sin salida encadenable (rti, sti, icfg = R...)
        uint8_t why = 0;
        // 1: la cadena vigila una interrupción pendiente (BurstStop::blockedPrio, o fuera de la ráfaga): sti e icfg = R vuelven
        // a la ráfaga, que mira irqReady. 0: nada pendiente (y lo pendiente solo cambia con un evento o un acceso con efectos,
        // que paran la cadena): siguen por su enlace
        uint8_t irqWatch = 1;
    };
    // enlace de una salida de un bloque a otro (en memoria de datos: enlazar no toca el código): to = la segunda entrada
    // del bloque de pc, o el epílogo (unlinked) mientras no se enlaza
    struct JitLink { void* to; void* unlinked; uint32_t pc; };
    using JitFn = void (*) (JitCtx*);
    struct JitBlock
    {
        uint32_t pc = 0xffffffffu, last = 0, n = 0;
        JitFn fn = nullptr;
        void* entryB = nullptr;  // la entrada encadenada (con las comprobaciones)
        uint64_t slotMask = 0;   // entradas de la tabla de bucles que podrían caer en sus pc
    };
    std::deque<JitLink> jitLinks;
    JitLink* jitPending = nullptr;  // salida sin enlazar del último bloque: se enlaza con el siguiente si es su pc
    void jitUnlinkAll() noexcept;
    // Saltos a destino calculado (rts, pop a pc, call R, tablas): pc -> entrada encadenada de su bloque, directa por
    // (pc >> 1) mod 2^14, como Jit::itab de fm1-emu. El código nativo la lee al final de esos bloques; la entrada
    // encadenada repite las comprobaciones de la ráfaga. 256 KiB. Se vacía con los enlaces (jitUnlinkAll) y con el búfer
    struct JitItab { uint32_t pc, pad; const void* body; };
    static constexpr uint32_t kItabBits = 14;
    std::vector<JitItab> jitItab;
    void jitItabClear() noexcept;
    // índice por media palabra de los primeros 2 MiB de la XIP (la aplicación de Felucca ocupa 586 KB): 0 sin mirar,
    // 1 sin bloque, k + 2 = jitBlocks[k]. 4 MiB; la consulta "sin bloque" no toca los bloques
    static constexpr uint32_t kJitSpan = 1u << 21, kJitMax = 64;
    std::vector<uint32_t> jitIndex;
    std::vector<uint32_t> jitIndexRam;  // lo mismo para la RAM de datos (código copiado ahí); se vacía si cambia codeWrites
    uint64_t jitRamCw = 0;
    std::vector<JitBlock> jitBlocks;
    std::deque<Line> jitLines;  // las líneas de los bloques (el código nativo apunta a ellas: no se mueven)
    std::unique_ptr<CodeBuffer> jitCode;
    bool jitFailed = false;     // sin memoria ejecutable o búfer lleno: el intérprete
    const JitBlock* jitAt (Bus& bus, uint32_t at);
    static int jitOp (JitCtx* c, uint32_t j, const Line* l);
    static void jitFpu (Cpu* c, uint32_t raw);  // una instrucción de la FPU (ruta 31) desde el código nativo
    static uint32_t jitIcfgRd (Cpu* c) noexcept { return c->icfg(); }  // R = icfg desde el código nativo
    // un par que jitEmit hace con los 16 registros: cabeza cruda (su registro destino va en el raw), o una llamada (FPU,
    // R = icfg) en cualquiera de las dos (pisa los registros volátiles que usa el par en nativo)
    static bool pairWide (const Line& head, const Line& second) noexcept;
    // Salida del bloque a mitad de la parte then de un if con else en línea: el estado que el intérprete tendría (el salto
    // pendiente en predSkips; o, acabada la parte then, el pc en elseEnd)
    static void jitThenExit (Cpu* c, uint32_t thenEnd, uint32_t elseEnd, uint32_t thenStart);
    // Salida del bloque en la última instrucción del cuerpo de un rep (que corre con repEnd = 0): repEnd de vuelta y, si
    // se hizo, el descuento de finishSlow
    static void jitRepExit (Cpu* c, uint32_t start, uint32_t end);
    bool jitNative (Emitter& e, const Line& l);  // una instrucción en código nativo (false: la hace jitOp)
    // (jitEmit) eax ya tiene el registro de off si la instrucción anterior acabó en mov [off], eax justo en la posición at
    // del emisor em (el del bloque; las sondas de jitAt usan otro y no lo tocan): la siguiente se ahorra la carga
    const Emitter* jitEaxEm = nullptr;
    size_t jitEaxAt = 0;
    uint32_t jitEaxOff = 0;
    // el salto que cierra el bloque, en nativo, con lo pendiente apuntado (pend nativas antes): false si no
    bool jitBranch (Emitter& e, const Line& l, uint32_t pend);
    // carga o almacenamiento con la RAM en línea; el resto de direcciones, por jitOp (j: índice en el bloque; pend y
    // pendPc: las nativas pendientes de apuntar antes de ella). false: esta forma no
    bool jitMem (Emitter& e, const Line& l, uint32_t j, uint32_t pend, uint32_t pendPc, std::vector<size_t>& exits);
    // push y pop de listas de registros con la RAM en línea (desde jitMem)
    bool jitList (Emitter& e, const Line& l, uint32_t j, uint32_t pend, uint32_t pendPc, std::vector<size_t>& exits);
    bool jitBlock (Emitter& e, const Line& l, uint32_t j, uint32_t pend, uint32_t pendPc, std::vector<size_t>& exits);
    bool jitPopRet (Emitter& e, const Line& l, uint32_t j, uint32_t pend, uint32_t pendPc, std::vector<size_t>& exits);
    // la ruta lenta común: jitOp con lo pendiente apuntado; slow, los saltos que llegan a ella; conts, los de "sigue"
    void jitSlow (Emitter& e, const Line& l, uint32_t j, uint32_t pend, uint32_t pendPc, std::vector<size_t>& exits,
                  const std::vector<size_t>& slow, std::initializer_list<size_t> conts);
    Bus::RamView jitRam {};
    bool jitRamOk = false;
    Bus::XipView jitXip {};
    bool jitXipOk = false;
    // el código de un bloque; devuelve dónde empieza la segunda entrada y añade sus enlaces (con el desplazamiento del
    // epílogo en "to", que jitAt convierte en dirección)
    size_t jitEmit (Emitter& e, const std::vector<const Line*>& lines, bool inRam, uint64_t slotMask, std::vector<JitLink*>& links);

    Line* fetch (Bus& bus, uint32_t at);
    bool execute (Bus& bus, const Line& l, uint32_t nextPc);
    bool execFast (Bus& bus, const Line& l, uint32_t nextPc);
    bool execRoute (Bus& bus, const Line& l, uint32_t nextPc);
    // Devuelve false si la forma no tiene semántica; jump/hasJump = salto tomado
    bool execSyntax (Bus& bus, const Line& l, uint32_t nextPc, bool& hasJump, uint32_t& jump);
    void execRaw (Bus& bus, uint8_t route, uint32_t raw);
    void finish (bool hasJump, uint32_t jump, uint32_t nextPc);
    void finishSlow (bool hasJump, uint32_t at, uint32_t nextPc);  // bloques if {} y rep en curso
    bool stepImpl (Bus& bus);
    Line* fetchSlow (Bus& bus, uint32_t at, Line& line, bool hit);
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
    uint32_t doRti() noexcept;  // rti: sale de la ISR en curso y devuelve el destino (reti)
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
