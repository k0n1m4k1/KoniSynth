// Emulación de alto nivel (HLE) de rutinas del firmware de fábrica del FM-1: se sustituye su código emulado por el
// equivalente nativo, comprobado bit a bit contra la emulación (Machine::Hle, modo Check). Solo las dos versiones más
// recientes de fábrica (otras: se emulan, o Check las deja apagadas).
// V15: el núcleo de operadores, FmCore::render de Synth_Dexed con el motor EngineMkI (User/FM1/SOUND-ENGINE.md), tal como
// lo compiló M-VAVE:
//  - FmOpParams de 16 B: +0 ganancia del bloque anterior, +4 nivel de la envolvente (Q24) que la rutina sustituye por la
//    ganancia nueva (ENV_MAX - nivel >> 14), +8 frecuencia, +12 fase;
//  - las cadenas con realimentación de los algoritmos 4 y 6 (compute_fb3/fb2) llevan su ganancia de +0 a la nueva con la
//    rampa de los demás operadores;
//  - el 0xC4 de esos algoritmos se escribe en la tabla de algoritmos global de la RAM, no en una copia.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fm1
{
// Dónde está FmCore::render en la RAM (el firmware copia el código caliente al arrancar) y su tabla de algoritmos
struct HleRenderSite
{
    uint32_t entry = 0, algTable = 0;
    explicit operator bool() const noexcept { return entry != 0; }
};

// Busca la rutina por su prólogo (V15: 0x01C01344) y lee la dirección de la tabla de la constante que la
// carga (+0x5C). Sin coincidencia, entry = 0
HleRenderSite findFmCoreRender (const uint8_t* ram, uint32_t ramBase, size_t ramSize) noexcept;

// Argumentos de la llamada: r0 salida (64 int32), r1 FmOpParams[6], r2 algoritmo, r3 fb_buf[2]; en la pila fb_shift y n
struct HleRenderArgs
{
    uint32_t out = 0, params = 0, algo = 0, fb = 0, fbShift = 0, n = 0, algTable = 0;
};

// Ejecuta la rutina sobre la RAM emulada. false (sin tocar nada) si algún dato cae fuera de la RAM, el algoritmo no es
// válido o n no es 64: entonces se emula
bool hleFmCoreRender (uint8_t* ram, uint32_t ramBase, size_t ramSize, const HleRenderArgs& a) noexcept;
} // namespace fm1
