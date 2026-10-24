#pragma once

// Osciladores del unísono en 8 carriles (AVX2). Interfaz sin JUCE ni plantillas: SimdAvx2.cpp se compila con
// /arch:AVX2 y no debe compartir funciones inline con el resto del plugin (el enlazador podría quedarse con la copia
// AVX2 y ejecutarla en una CPU sin AVX2).
namespace konisimd
{
constexpr int kLanes = 8;

// Estado del unísono de una voz. Carriles inactivos: ganancia 0 y relación 1 (se calculan igualmente).
struct UnisonLanes
{
    alignas (32) double phase[kLanes];
    alignas (32) double modPhase[kLanes];
    alignas (32) double ratio[kLanes];
    alignas (32) float gainL[kLanes];
    alignas (32) float gainR[kLanes];
    alignas (32) double inc[kLanes]; // último incremento de fase de la portadora (con FM; con signo). Lo escribe el paso complejo
    alignas (32) double mod[kLanes]; // último valor de la moduladora (para escalar los saltos por la AM)
};

struct ComplexArgs
{
    int wave, modWave;
    double modRatio, fmIndex, am, pw;
    bool sync;
    // Modulador externo (complexModSource = Osc 2 u Osc 3): un mismo valor para todos los carriles en esta submuestra, en
    // lugar del modulador interno (modWave, modRatio y modPhase por carril, que entonces no avanza)
    bool external = false;
    double extMod = 0.0;      // valor del modulador
    double extDtM = 0.0;      // su incremento de fase en esta submuestra (> 0)
    bool extWrapped = false;  // dio la vuelta (o lo reinició su propio sync) dentro de esta submuestra
    double extPhaseEnd = 0.0; // su fase al final de la submuestra: tras una vuelta, lo recorrido desde ella
};

// Una muestra del unísono (sin modo complejo): suma estéreo y avance de fases. Misma semántica que la ruta escalar.
// La portadora sale sin corregir (forma ingenua): el antialiasing lo añade el minBLEP de la voz (Dsp/MinBlep.h).
void unisonStepAvx2 (UnisonLanes& lanes, int wave, double dt, double pw, float& left, float& right);

// Una submuestra del oscilador complejo (dtP ya incluye el factor 1/2 del sobremuestreo 2x).
void complexStepAvx2 (UnisonLanes& lanes, const ComplexArgs& args, double dtP, float& left, float& right, float& firstMod);

// FM 6-Op: un carril por voz (8 voces). Operador k = 0..5 en el orden de fm_core.cc (0 = OP6).
struct FmLanes
{
    alignas (32) float phase[6][kLanes];
    alignas (32) float inc[6][kLanes];     // ciclos por muestra, sin LFO ni pitch bend
    alignas (32) float amp[6][kLanes];     // ganancia actual: ciclos de desviación (máximo 2 = 4π rad)
    alignas (32) float ampStep[6][kLanes]; // rampa de ganancia por muestra dentro del subbloque
    alignas (32) float y1[6][kLanes];      // salidas anteriores (realimentación)
    alignas (32) float y2[6][kLanes];
};

struct FmArgs
{
    unsigned char flags[6]; // banderas de fm_core.cc: bus de entrada (bits 4-5), de salida (0-1), suma (2)
    int fbSrc, fbDest;      // realimentación: salida de fbSrc hacia la entrada de fbDest (iguales salvo en los algoritmos 4 y 6)
    float fbScale, pitchMul;
    bool fixed[6];          // frecuencia fija: no la afecta pitchMul
};

// numSamples muestras (<= 16): out[i] = suma de las portadoras de los 8 carriles
void fmRenderAvx2 (FmLanes& lanes, const FmArgs& args, int numSamples, float* out);
} // namespace konisimd
