// Paquetes de actualización del FM-1 (.fwsc) y la flash en que se convierten.
//
// Port a C++ de fm1-emu (Joakim Eriksson, licencia MIT, commit 3e3fb27), crates/fm1-soc/src/fwsc.rs. Formato: bloques de
// cabecera enmascarados que nombran la imagen de flash (20 en el FM-1; más en los paquetes del SMK-37); dentro, un
// directorio JLFS de cabezas de 32 bytes enmascaradas y el área de la aplicación, cifrada por bloques de 32 bytes con la
// clave del chip. unpack devuelve la flash tal como la escribe el actualizador (lo que lee el lado SPI) y el área de la
// aplicación descifrada (lo que ve la CPU por la XIP, donde el SFC descifra al vuelo), con su directorio.
//
// El firmware de fábrica guarda sus patches en el área USR (flash 0xEA000..0xFC000 en la V13 del FM-1), que las
// actualizaciones no tocan: un aparato real lleva ahí el banco de fábrica. usrFillDefaults pone en un área vacía el
// banco pequeño propio de fm1-emu; usrLoadSyx, un banco DX7 de 32 voces.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fm1
{
struct FwscHead  // cabeza JLFS de 32 bytes: offset +4, size +8, flags +12, last +14, name +16
{
    std::string name;
    size_t offset = 0, size = 0;
    uint8_t flags = 0;
    bool last = false;
};

struct FwscUnpacked
{
    std::string identity;       // lo que deletrean los marcadores de la cabecera ("FM-1_015", "SMK-37 Pro_016")
    std::vector<uint8_t> raw;   // la imagen de flash byte a byte, como la escribe el actualizador
    std::vector<uint8_t> xip;   // el área de la aplicación descifrada desde su inicio: el contenido de la ventana XIP
    size_t area = 0;            // dónde empieza el área en la flash (XIP 0x02000000 apunta aquí)
    size_t size = 0;            // tamaño del área (el de su cabeza)
    // Dónde acaba la imagen protegida (la entrada PRCT del área; si no, el final del área): descifrada hasta aquí.
    // Tras el área va el directorio EXT_RESERVED (la V16 encuentra por él su banco USER en 0xED000).
    size_t end = 0;
    std::vector<FwscHead> dir;  // directorio del área: app.bin (offset en el área), VM, PRCT, BTIF, USR (offsets de flash)
    uint16_t key = 0;           // clave del chip con que está cifrada el área (el SFC descifra con ella)

    const FwscHead* entry (const std::string& name) const noexcept;
};

void fwscUnmask (uint8_t* buf, size_t n, uint16_t key) noexcept;
uint16_t fwscCrc16 (const uint8_t* buf, size_t n) noexcept;

// Cabecera: bloques de 0x30 bytes cuyo último byte es un marcador, identity[i] + i + 1 mientras dura la identidad y 0x7D
// después. Devuelve el número de bloques y la identidad.
size_t fwscContainer (const std::vector<uint8_t>& file, std::string& identity);

// false y error si no es un paquete válido
bool fwscUnpack (const std::vector<uint8_t>& file, FwscUnpacked& out, std::string& error);

// Disposición del área USR por placa: voces de 128 bytes (formato DX7 VCED empaquetado de 128) y ajustes por ranura
struct UsrLayout
{
    size_t voices, settings, settingsLen, tail;
    static const UsrLayout fm1, smk37;
};

std::vector<uint8_t> fwscFullFlash (const std::vector<uint8_t>& raw, size_t size);  // rellena con 0xff hasta size
bool usrFillDefaults (std::vector<uint8_t>& flash, size_t usr, const UsrLayout& layout);
// Devuelve las voces escritas (32 como máximo) o -1 con error
int usrLoadSyx (std::vector<uint8_t>& flash, size_t usr, const UsrLayout& layout, const std::vector<uint8_t>& syx, size_t first,
                std::string& error);
} // namespace fm1
