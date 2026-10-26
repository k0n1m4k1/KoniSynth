// Ventana maximizada del standalone en Windows con la barra de tareas a la izquierda o arriba. JUCE 9.0.4, ventana sin barra
// de título nativa (juce_Windowing_windows.cpp, WM_NCCALCSIZE): al maximizar recorta el margen respecto al borde del
// MONITOR (rcMonitor) y no del área de trabajo (rcWork). Con la barra a la izquierda (72 px físicos) el rectángulo propuesto
// empieza en x = 61, padX = 0 - 61 = -61 y el área cliente pasa a [0, 2632] en un monitor de 2560: el borde derecho del
// editor (PANIC...) quedaba fuera (medido en Windows 11, 2560 × 1600 al 150 %). Arreglo sin tocar JUCE: un procedimiento de
// ventana que llama al de JUCE y, si la ventana está maximizada y su área cliente sale del área de trabajo de su monitor, la
// ajusta a ella. Fichero aparte para que windows.h no entre en la unidad del plugin (macros min/max, etc.)
#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

namespace
{
HWND hookedWindow = nullptr;
WNDPROC juceProc = nullptr;

LRESULT CALLBACK fixedProc (HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const auto result = CallWindowProcW (juceProc, hwnd, msg, wParam, lParam);
    if (msg == WM_NCCALCSIZE && IsZoomed (hwnd))
    {
        auto* client = reinterpret_cast<RECT*> (lParam); // con wParam TRUE, el primer RECT de NCCALCSIZE_PARAMS
        MONITORINFO info {};
        info.cbSize = sizeof (info);
        if (GetMonitorInfoW (MonitorFromWindow (hwnd, MONITOR_DEFAULTTONEAREST), &info))
        {
            const auto& work = info.rcWork;
            if (client->left < work.left || client->top < work.top || client->right > work.right || client->bottom > work.bottom)
                *client = work;
        }
    }
    return result;
}
} // namespace

// Engancha (una vez por ventana) o suelta el procedimiento; nativeHandle = HWND de la ventana del standalone
void koniHookMaximiseFix (void* nativeHandle)
{
    auto* hwnd = static_cast<HWND> (nativeHandle);
    if (hwnd == nullptr || hwnd == hookedWindow)
        return;
    if (hookedWindow != nullptr && IsWindow (hookedWindow)
        && GetWindowLongPtrW (hookedWindow, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR> (&fixedProc))
        SetWindowLongPtrW (hookedWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR> (juceProc));
    juceProc = reinterpret_cast<WNDPROC> (SetWindowLongPtrW (hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR> (&fixedProc)));
    hookedWindow = hwnd;
    // Recalcula el marco por si ya estaba maximizada (estado restaurado)
    SetWindowPos (hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void koniUnhookMaximiseFix (void* nativeHandle)
{
    auto* hwnd = static_cast<HWND> (nativeHandle);
    if (hwnd == nullptr || hwnd != hookedWindow)
        return;
    if (IsWindow (hwnd) && GetWindowLongPtrW (hwnd, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR> (&fixedProc))
        SetWindowLongPtrW (hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR> (juceProc));
    hookedWindow = nullptr;
    juceProc = nullptr;
}
#else
void koniHookMaximiseFix (void*) {}
void koniUnhookMaximiseFix (void*) {}
#endif
