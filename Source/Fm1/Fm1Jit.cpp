// Búfer de código del JIT (ver Fm1Jit.h): memoria virtual del sistema, W^X
#include "Fm1Jit.h"

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#elif defined(__linux__)
 #include <sys/mman.h>
 #include <unistd.h>
#endif

namespace fm1
{
bool preferPerformanceCores() noexcept
{
#if defined(_WIN32)
    // SetThreadInformation (ThreadPowerThrottling = 3) con THREAD_POWER_THROTTLING_STATE { versión 1, máscara
    // EXECUTION_SPEED = 1, estado 0: sin limitar } (processthreadsapi.h, documentación de Microsoft). Por GetProcAddress:
    // la función existe desde Windows 8 y la clase desde Windows 10 1709; en uno anterior falla y no pasa nada
    struct State { ULONG version, controlMask, stateMask; } state { 1, 1, 0 };
    using Fn = BOOL (WINAPI*) (HANDLE, int, LPVOID, DWORD);
    const auto kernel = GetModuleHandleW (L"kernel32.dll");
    const auto fn = kernel != nullptr ? reinterpret_cast<Fn> (reinterpret_cast<void*> (GetProcAddress (kernel, "SetThreadInformation")))
                                      : nullptr;
    return fn != nullptr && fn (GetCurrentThread(), 3, &state, (DWORD) sizeof state) != 0;
#else
    return false;
#endif
}

namespace
{
constexpr size_t kPage = 4096;  // (las dos plataformas con JIT usan páginas de 4 KiB para esta memoria)

bool protect (uint8_t* p, size_t n, bool exec) noexcept
{
#if defined(_WIN32)
    DWORD old = 0;
    return VirtualProtect (p, n, exec ? PAGE_EXECUTE_READ : PAGE_READWRITE, &old) != 0;
#elif defined(__linux__)
    return mprotect (p, n, exec ? (PROT_READ | PROT_EXEC) : (PROT_READ | PROT_WRITE)) == 0;
#else
    (void) p; (void) n; (void) exec;
    return false;
#endif
}
} // namespace

CodeBuffer::~CodeBuffer()
{
    if (base == nullptr)
        return;
#if defined(_WIN32)
    VirtualFree (base, 0, MEM_RELEASE);
#elif defined(__linux__)
    munmap (base, size);
#endif
}

bool CodeBuffer::init (size_t bytes)
{
    if (! jitSupported() || base != nullptr)
        return base != nullptr;
    bytes = (bytes + kPage - 1) & ~(kPage - 1);
#if defined(_WIN32)
    base = (uint8_t*) VirtualAlloc (nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(__linux__)
    void* p = mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    base = p == MAP_FAILED ? nullptr : (uint8_t*) p;
#endif
    if (base == nullptr)
        return false;
    size = bytes;
    top = 0;
    return protect (base, size, true);  // nada escribible hasta que se añada código
}

void* CodeBuffer::add (const uint8_t* code, size_t n)
{
    if (base == nullptr || n == 0)
        return nullptr;
    const size_t at = (top + 15) & ~(size_t) 15;  // bloques alineados a 16 B
    if (at + n > size)
        return nullptr;
    // las páginas que toca, en escritura mientras se copia
    const size_t p0 = at & ~(kPage - 1), p1 = (at + n + kPage - 1) & ~(kPage - 1);
    if (! protect (base + p0, p1 - p0, false))
        return nullptr;
    std::memcpy (base + at, code, n);
    const bool ok = protect (base + p0, p1 - p0, true);
#if defined(_WIN32)
    FlushInstructionCache (GetCurrentProcess(), base + at, n);
#endif
    if (! ok)
        return nullptr;
    top = at + n;
    return base + at;
}
} // namespace fm1
