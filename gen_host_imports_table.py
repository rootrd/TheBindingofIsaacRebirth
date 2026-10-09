"""Generate host_ohos_imports.h — the sorted import dispatch table for host_ohos.c.

Reads the PE import table, matches each import to a handler function
(implemented or stub), and emits a sorted C table.
"""
import json
import sys

sys.path.insert(0, r'F:\TheBindingofIsaacRebirth\repentogxm\recomp')
from iat_meta import read_imports

EXE = r'C:\Users\Administrator\Desktop\The Binding of Isaac Rebirth 1.7.9b\isaac-ng.exe.unpacked.exe'
OUTPUT = r'F:\TheBindingofIsaacRebirth\isaac-ohos\entry\src\main\cpp\isaac\generated\host_imports_table.h'

# Handler assignments: "DLL!symbol" -> "function_name"
# Unassigned imports get "hDUMMY" (returns 0, cdecl)
HANDLERS = {
    # ADVAPI32
    "ADVAPI32.dll!OpenProcessToken": "h_advapi32_stub",
    "ADVAPI32.dll!LookupPrivilegeValueA": "h_advapi32_stub",
    "ADVAPI32.dll!AdjustTokenPrivileges": "h_advapi32_stub",

    # GDI32
    "GDI32.dll!SetPixelFormat": "h_SetPixelFormat",
    "GDI32.dll!GetDeviceCaps": "h_GetDeviceCaps",
    "GDI32.dll!DeleteDC": "h_DeleteDC",
    "GDI32.dll!DescribePixelFormat": "h_DescribePixelFormat",
    "GDI32.dll!SwapBuffers": "h_SwapBuffers",
    "GDI32.dll!CreateDCA": "h_CreateDCA",

    # KERNEL32 — critical
    "KERNEL32.dll!InitializeSListHead": "h_InitializeSListHead",
    "KERNEL32.dll!IsProcessorFeaturePresent": "h_IsProcessorFeaturePresent",
    "KERNEL32.dll!TerminateProcess": "h_TerminateProcess",
    "KERNEL32.dll!GetProcessTimes": "h_GetProcessTimes",
    "KERNEL32.dll!SetUnhandledExceptionFilter": "h_SetUnhandledExceptionFilter",
    "KERNEL32.dll!GetCurrentProcess": "h_GetCurrentProcess",
    "KERNEL32.dll!GetCurrentProcessId": "h_GetCurrentProcessId",
    "KERNEL32.dll!GetCurrentThread": "h_GetCurrentThread",
    "KERNEL32.dll!GetCurrentThreadId": "h_GetCurrentThreadId",
    "KERNEL32.dll!GetTickCount": "h_GetTickCount",
    "KERNEL32.dll!QueryPerformanceCounter": "h_QueryPerformanceCounter",
    "KERNEL32.dll!QueryPerformanceFrequency": "h_QueryPerformanceFrequency",
    "KERNEL32.dll!GetSystemTime": "h_GetSystemTime",
    "KERNEL32.dll!GetLocalTime": "h_GetLocalTime",
    "KERNEL32.dll!GetEnvironmentVariableA": "h_GetEnvironmentVariableA",
    "KERNEL32.dll!GetModuleHandleA": "h_GetModuleHandleA",
    "KERNEL32.dll!GetModuleFileNameA": "h_GetModuleFileNameA",
    "KERNEL32.dll!GetProcAddress": "h_GetProcAddress",
    "KERNEL32.dll!LoadLibraryA": "h_LoadLibraryA",
    "KERNEL32.dll!FreeLibrary": "h_FreeLibrary",
    "KERNEL32.dll!VirtualAlloc": "h_VirtualAlloc",
    "KERNEL32.dll!VirtualFree": "h_VirtualFree",
    "KERNEL32.dll!VirtualProtect": "h_VirtualProtect",
    "KERNEL32.dll!GetProcessHeap": "h_GetProcessHeap",
    "KERNEL32.dll!HeapAlloc": "h_HeapAlloc",
    "KERNEL32.dll!HeapFree": "h_HeapFree",
    "KERNEL32.dll!HeapCreate": "h_HeapCreate",
    "KERNEL32.dll!HeapDestroy": "h_HeapDestroy",
    "KERNEL32.dll!HeapReAlloc": "h_HeapReAlloc",
    "KERNEL32.dll!HeapSize": "h_HeapSize",
    "KERNEL32.dll!HeapValidate": "h_HeapValidate",
    "KERNEL32.dll!GetLastError": "h_GetLastError",
    "KERNEL32.dll!SetLastError": "h_SetLastError",
    "KERNEL32.dll!CreateFileA": "h_CreateFileA",
    "KERNEL32.dll!ReadFile": "h_ReadFile",
    "KERNEL32.dll!WriteFile": "h_WriteFile",
    "KERNEL32.dll!CloseHandle": "h_CloseHandle",
    "KERNEL32.dll!GetFileSize": "h_GetFileSize",
    "KERNEL32.dll!SetFilePointer": "h_SetFilePointer",
    "KERNEL32.dll!FlushFileBuffers": "h_FlushFileBuffers",
    "KERNEL32.dll!GetFileAttributesA": "h_GetFileAttributesA",
    "KERNEL32.dll!CreateDirectoryA": "h_CreateDirectoryA",
    "KERNEL32.dll!RemoveDirectoryA": "h_RemoveDirectoryA",
    "KERNEL32.dll!DeleteFileA": "h_DeleteFileA",
    "KERNEL32.dll!MoveFileA": "h_MoveFileA",
    "KERNEL32.dll!FindFirstFileA": "h_FindFirstFileA",
    "KERNEL32.dll!FindNextFileA": "h_FindNextFileA",
    "KERNEL32.dll!FindClose": "h_FindClose",
    "KERNEL32.dll!MultiByteToWideChar": "h_MultiByteToWideChar",
    "KERNEL32.dll!WideCharToMultiByte": "h_WideCharToMultiByte",
    "KERNEL32.dll!GetSystemTimeAsFileTime": "h_GetSystemTimeAsFileTime",
    "KERNEL32.dll!GetTimeZoneInformation": "h_GetTimeZoneInformation",
    "KERNEL32.dll!Sleep": "h_Sleep",
    "KERNEL32.dll!GetStdHandle": "h_GetStdHandle",
    "KERNEL32.dll!SetConsoleCtrlHandler": "h_SetConsoleCtrlHandler",
    "KERNEL32.dll!ExitProcess": "h_ExitProcess",
    "KERNEL32.dll!GetConsoleMode": "h_GetConsoleMode",
    "KERNEL32.dll!SetConsoleMode": "h_SetConsoleMode",
    "KERNEL32.dll!GetFileType": "h_GetFileType",
    "KERNEL32.dll!GetStartupInfoA": "h_GetStartupInfoA",
    "KERNEL32.dll!GetCommandLineA": "h_GetCommandLineA",
    "KERNEL32.dll!GetVersionExA": "h_GetVersionExA",
    "KERNEL32.dll!GetSystemInfo": "h_GetSystemInfo",
    "KERNEL32.dll!GlobalMemoryStatusEx": "h_GlobalMemoryStatusEx",
    "KERNEL32.dll!OutputDebugStringA": "h_OutputDebugStringA",
    "KERNEL32.dll!RaiseException": "h_RaiseException",
    "KERNEL32.dll!UnhandledExceptionFilter": "h_UnhandledExceptionFilter",
    "KERNEL32.dll!SetHandleCount": "h_SetHandleCount",
    "KERNEL32.dll!FlushInstructionCache": "h_FlushInstructionCache",
    "KERNEL32.dll!lstrlenA": "h_lstrlenA",
    "KERNEL32.dll!lstrcpyA": "h_lstrcpyA",
    "KERNEL32.dll!lstrcpynA": "h_lstrcpynA",
    "KERNEL32.dll!lstrcmpA": "h_lstrcmpA",
    "KERNEL32.dll!lstrcmpiA": "h_lstrcmpiA",
    "KERNEL32.dll!WaitForSingleObject": "h_WaitForSingleObject",
    "KERNEL32.dll!WaitForMultipleObjects": "h_WaitForMultipleObjects",
    "KERNEL32.dll!CreateThread": "h_CreateThread",
    "KERNEL32.dll!ResumeThread": "h_ResumeThread",
    "KERNEL32.dll!SuspendThread": "h_SuspendThread",
    "KERNEL32.dll!TerminateThread": "h_TerminateThread",
    "KERNEL32.dll!GetExitCodeThread": "h_GetExitCodeThread",
    "KERNEL32.dll!SetThreadPriority": "h_SetThreadPriority",
    "KERNEL32.dll!GetThreadPriority": "h_GetThreadPriority",
    "KERNEL32.dll!LeaveCriticalSection": "h_LeaveCriticalSection",
    "KERNEL32.dll!EnterCriticalSection": "h_EnterCriticalSection",
    "KERNEL32.dll!InitializeCriticalSection": "h_InitializeCriticalSection",
    "KERNEL32.dll!DeleteCriticalSection": "h_DeleteCriticalSection",
    "KERNEL32.dll!TryEnterCriticalSection": "h_TryEnterCriticalSection",
    "KERNEL32.dll!CreateMutexA": "h_CreateMutexA",
    "KERNEL32.dll!ReleaseMutex": "h_ReleaseMutex",
    "KERNEL32.dll!CreateEventA": "h_CreateEventA",
    "KERNEL32.dll!SetEvent": "h_SetEvent",
    "KERNEL32.dll!ResetEvent": "h_ResetEvent",
    "KERNEL32.dll!InterlockedIncrement": "h_InterlockedIncrement",
    "KERNEL32.dll!InterlockedDecrement": "h_InterlockedDecrement",
    "KERNEL32.dll!InterlockedExchange": "h_InterlockedExchange",
    "KERNEL32.dll!InterlockedExchangeAdd": "h_InterlockedExchangeAdd",
    "KERNEL32.dll!InterlockedCompareExchange": "h_InterlockedCompareExchange",

    # OPENGL32
    "OPENGL32.dll!wglCreateContext": "h_wglCreateContext",
    "OPENGL32.dll!wglDeleteContext": "h_wglDeleteContext",
    "OPENGL32.dll!wglMakeCurrent": "h_wglMakeCurrent",
    "OPENGL32.dll!wglGetProcAddress": "h_wglGetProcAddress",
    "OPENGL32.dll!wglShareLists": "h_wglShareLists",

    # SHELL32
    "SHELL32.dll!SHFileOperationA": "h_shell32_stub",

    # USER32 — critical for window/input
    "USER32.dll!BringWindowToTop": "h_BringWindowToTop",
    "USER32.dll!SetCapture": "h_SetCapture",
    "USER32.dll!SetCursor": "h_SetCursor",
    "USER32.dll!SetFocus": "h_SetFocus",
    "USER32.dll!ClipCursor": "h_ClipCursor",
    "USER32.dll!GetCursorPos": "h_GetCursorPos",
    "USER32.dll!SetCursorPos": "h_SetCursorPos",
    "USER32.dll!ShowCursor": "h_ShowCursor",
    "USER32.dll!GetMessageA": "h_GetMessageA",
    "USER32.dll!PeekMessageA": "h_PeekMessageA",
    "USER32.dll!DispatchMessageA": "h_DispatchMessageA",
    "USER32.dll!TranslateMessage": "h_TranslateMessage",
    "USER32.dll!PostQuitMessage": "h_PostQuitMessage",
    "USER32.dll!MessageBoxA": "h_MessageBoxA",
    "USER32.dll!GetForegroundWindow": "h_GetForegroundWindow",
    "USER32.dll!SetForegroundWindow": "h_SetForegroundWindow",
    "USER32.dll!GetDesktopWindow": "h_GetDesktopWindow",
    "USER32.dll!GetWindowRect": "h_GetWindowRect",
    "USER32.dll!GetClientRect": "h_GetClientRect",
    "USER32.dll!AdjustWindowRect": "h_AdjustWindowRect",
    "USER32.dll!CreateWindowExA": "h_CreateWindowExA",
    "USER32.dll!DestroyWindow": "h_DestroyWindow",
    "USER32.dll!ShowWindow": "h_ShowWindow",
    "USER32.dll!UpdateWindow": "h_UpdateWindow",
    "USER32.dll!SetWindowLongA": "h_SetWindowLongA",
    "USER32.dll!GetWindowLongA": "h_GetWindowLongA",
    "USER32.dll!DefWindowProcA": "h_DefWindowProcA",
    "USER32.dll!RegisterClassExA": "h_RegisterClassExA",
    "USER32.dll!UnregisterClassA": "h_UnregisterClassA",
    "USER32.dll!SetWindowPos": "h_SetWindowPos",
    "USER32.dll!ScreenToClient": "h_ScreenToClient",
    "USER32.dll!ClientToScreen": "h_ClientToScreen",
    "USER32.dll!GetSystemMetrics": "h_GetSystemMetrics",
    "USER32.dll!ReleaseCapture": "h_ReleaseCapture",
    "USER32.dll!GetKeyState": "h_GetKeyState",
    "USER32.dll!GetAsyncKeyState": "h_GetAsyncKeyState",
    "USER32.dll!GetKeyboardState": "h_GetKeyboardState",
    "USER32.dll!MapVirtualKeyA": "h_MapVirtualKeyA",
    "USER32.dll!ToAsciiEx": "h_ToAsciiEx",
    "USER32.dll!GetKeyNameTextA": "h_GetKeyNameTextA",
    "USER32.dll!EnumDisplaySettingsA": "h_EnumDisplaySettingsA",
    "USER32.dll!ChangeDisplaySettingsA": "h_ChangeDisplaySettingsA",
    "USER32.dll!SetWindowTextA": "h_SetWindowTextA",
    "USER32.dll!GetWindowTextA": "h_GetWindowTextA",
    "USER32.dll!GetActiveWindow": "h_GetActiveWindow",
    "USER32.dll!SetActiveWindow": "h_SetActiveWindow",
    "USER32.dll!IsWindowVisible": "h_IsWindowVisible",
    "USER32.dll!IsWindow": "h_IsWindow",
    "USER32.dll!InvalidateRect": "h_InvalidateRect",
    "USER32.dll!ValidateRect": "h_ValidateRect",
    "USER32.dll!RedrawWindow": "h_RedrawWindow",
    "USER32.dll!BeginPaint": "h_BeginPaint",
    "USER32.dll!EndPaint": "h_EndPaint",
    "USER32.dll!GetDC": "h_GetDC",
    "USER32.dll!ReleaseDC": "h_ReleaseDC",

    # VCRUNTIME140
    "VCRUNTIME140.dll!__RTDynamicCast": "h__RTDynamicCast",
    "VCRUNTIME140.dll!_except_handler4_common": "h__except_handler4_common",
    "VCRUNTIME140.dll!_setjmp3": "h_crt__setjmp3",
    "VCRUNTIME140.dll!strchr": "h_strchr_vcruntime",

    # WINMM
    "WINMM.dll!timeEndPeriod": "h_winmm_stub",
    "WINMM.dll!timeGetDevCaps": "h_winmm_stub",
    "WINMM.dll!timeBeginPeriod": "h_winmm_stub",
    "WINMM.dll!timeGetTime": "h_winmm_stub",

    # CRT heap
    "api-ms-win-crt-heap-l1-1-0.dll!malloc": "h_crt_malloc",
    "api-ms-win-crt-heap-l1-1-0.dll!free": "h_crt_free",
    "api-ms-win-crt-heap-l1-1-0.dll!calloc": "h_crt_calloc",
    "api-ms-win-crt-heap-l1-1-0.dll!realloc": "h_crt_realloc",
    "api-ms-win-crt-heap-l1-1-0.dll!_callnewh": "h_crt__callnewh",
    "api-ms-win-crt-heap-l1-1-0.dll!_set_new_mode": "h_crt__set_new_mode",

    # CRT math
    "api-ms-win-crt-math-l1-1-0.dll!ceil": "h_crt_ceil",
    "api-ms-win-crt-math-l1-1-0.dll!floor": "h_crt_floor",
    "api-ms-win-crt-math-l1-1-0.dll!ldexp": "h_crt_ldexp",
    "api-ms-win-crt-math-l1-1-0.dll!copysignf": "h_crt_copysignf",
    "api-ms-win-crt-math-l1-1-0.dll!sqrt": "h_crt_sqrt",
    "api-ms-win-crt-math-l1-1-0.dll!fmod": "h_crt_fmod",
    "api-ms-win-crt-math-l1-1-0.dll!pow": "h_crt_pow",
    "api-ms-win-crt-math-l1-1-0.dll!sin": "h_crt_sin",
    "api-ms-win-crt-math-l1-1-0.dll!cos": "h_crt_cos",
    "api-ms-win-crt-math-l1-1-0.dll!tan": "h_crt_tan",
    "api-ms-win-crt-math-l1-1-0.dll!atan": "h_crt_atan",
    "api-ms-win-crt-math-l1-1-0.dll!atan2": "h_crt_atan2",
    "api-ms-win-crt-math-l1-1-0.dll!asin": "h_crt_asin",
    "api-ms-win-crt-math-l1-1-0.dll!acos": "h_crt_acos",
    "api-ms-win-crt-math-l1-1-0.dll!exp": "h_crt_exp",
    "api-ms-win-crt-math-l1-1-0.dll!log": "h_crt_log",
    "api-ms-win-crt-math-l1-1-0.dll!log10": "h_crt_log10",
    "api-ms-win-crt-math-l1-1-0.dll!fabs": "h_crt_fabs",
    "api-ms-win-crt-math-l1-1-0.dll!modf": "h_crt_modf",
    "api-ms-win-crt-math-l1-1-0.dll!frexp": "h_crt_frexp",
    "api-ms-win-crt-math-l1-1-0.dll!_fdclass": "h_crt__fdclass",

    # CRT runtime
    "api-ms-win-crt-runtime-l1-1-0.dll!_controlfp_s": "h_crt__controlfp_s",
    "api-ms-win-crt-runtime-l1-1-0.dll!_initterm": "h_crt__initterm",
    "api-ms-win-crt-runtime-l1-1-0.dll!_initterm_e": "h_crt__initterm_e",
    "api-ms-win-crt-runtime-l1-1-0.dll!_get_initial_narrow_environment": "h_crt__get_initial_narrow_environment",
    "api-ms-win-crt-runtime-l1-1-0.dll!_register_thread_local_exe_atexit_callback": "h_crt__register_thread_local_exe_atexit_callback",
    "api-ms-win-crt-runtime-l1-1-0.dll!_initialize_onexit_table": "h_crt__initialize_onexit_table",
    "api-ms-win-crt-runtime-l1-1-0.dll!_register_onexit_function": "h_crt__register_onexit_function",
    "api-ms-win-crt-runtime-l1-1-0.dll!_execute_onexit_table": "h_crt__execute_onexit_table",
    "api-ms-win-crt-runtime-l1-1-0.dll!_crt_atexit": "h_crt__crt_atexit",
    "api-ms-win-crt-runtime-l1-1-0.dll!abort": "h_crt_abort",
    "api-ms-win-crt-runtime-l1-1-0.dll!terminate": "h_crt_abort",

    # CRT string
    "api-ms-win-crt-string-l1-1-0.dll!_strdup": "h_crt__strdup",
    "api-ms-win-crt-string-l1-1-0.dll!_strnicmp": "h_crt__strnicmp",
    "api-ms-win-crt-string-l1-1-0.dll!_stricmp": "h_crt__stricmp",
    "api-ms-win-crt-string-l1-1-0.dll!strpbrk": "h_crt_strpbrk",
    "api-ms-win-crt-string-l1-1-0.dll!strchr": "h_crt_strchr",
    "api-ms-win-crt-string-l1-1-0.dll!strrchr": "h_crt_strrchr",
    "api-ms-win-crt-string-l1-1-0.dll!strstr": "h_crt_strstr",
    "api-ms-win-crt-string-l1-1-0.dll!strlen": "h_crt_strlen",
    "api-ms-win-crt-string-l1-1-0.dll!strcmp": "h_crt_strcmp",
    "api-ms-win-crt-string-l1-1-0.dll!strcpy": "h_crt_strcpy",
    "api-ms-win-crt-string-l1-1-0.dll!strncmp": "h_crt_strncmp",
    "api-ms-win-crt-string-l1-1-0.dll!strncpy": "h_crt_strncpy",
    "api-ms-win-crt-string-l1-1-0.dll!tolower": "h_crt_tolower",
    "api-ms-win-crt-string-l1-1-0.dll!toupper": "h_crt_toupper",
    "api-ms-win-crt-string-l1-1-0.dll!ispunct": "h_crt_ispunct",
    "api-ms-win-crt-string-l1-1-0.dll!isspace": "h_crt_isspace",
    "VCRUNTIME140.dll!memchr": "h_vcruntime_memchr",
    "VCRUNTIME140.dll!longjmp": "h_vcruntime_longjmp",
    "VCRUNTIME140.dll!memcpy": "h_vcruntime_memcpy",
    "VCRUNTIME140.dll!memmove": "h_vcruntime_memmove",
    "VCRUNTIME140.dll!memset": "h_vcruntime_memset",
    "VCRUNTIME140.dll!strstr": "h_vcruntime_strstr",
    "api-ms-win-crt-string-l1-1-0.dll!isdigit": "h_crt_isdigit",
    "api-ms-win-crt-string-l1-1-0.dll!isalpha": "h_crt_isalpha",
    "api-ms-win-crt-string-l1-1-0.dll!isalnum": "h_crt_isalnum",
    "api-ms-win-crt-string-l1-1-0.dll!isupper": "h_crt_isupper",
    "api-ms-win-crt-string-l1-1-0.dll!islower": "h_crt_islower",
    "api-ms-win-crt-string-l1-1-0.dll!isxdigit": "h_crt_isxdigit",
    "api-ms-win-crt-string-l1-1-0.dll!strcat": "hDUMMY",
    "api-ms-win-crt-string-l1-1-0.dll!strncat": "hDUMMY",

    # CRT convert
    "api-ms-win-crt-convert-l1-1-0.dll!atoi": "h_crt_atoi",
    "api-ms-win-crt-convert-l1-1-0.dll!strtoul": "h_crt_strtoul",
    "api-ms-win-crt-convert-l1-1-0.dll!strtoull": "h_crt_strtoull",
    "api-ms-win-crt-convert-l1-1-0.dll!strtoll": "h_crt_strtoll",
    "api-ms-win-crt-convert-l1-1-0.dll!mbstowcs_s": "h_crt_mbstowcs_s",
    "api-ms-win-crt-convert-l1-1-0.dll!wcstombs_s": "h_crt_wcstombs_s",

    # CRT environment
    "api-ms-win-crt-environment-l1-1-0.dll!getenv": "h_crt_getenv",

    # CRT filesystem
    "api-ms-win-crt-filesystem-l1-1-0.dll!remove": "h_crt_remove",
    "api-ms-win-crt-filesystem-l1-1-0.dll!_access": "h_crt__access",

    # CRT locale
    "api-ms-win-crt-locale-l1-1-0.dll!_configthreadlocale": "h_crt__configthreadlocale",

    # CRT stdio
    "api-ms-win-crt-stdio-l1-1-0.dll!fread": "h_crt_fread",
    "api-ms-win-crt-stdio-l1-1-0.dll!fwrite": "h_crt_fwrite",
    "api-ms-win-crt-stdio-l1-1-0.dll!fseek": "h_crt_fseek",
    "api-ms-win-crt-stdio-l1-1-0.dll!ftell": "h_crt_ftell",
    "api-ms-win-crt-stdio-l1-1-0.dll!_fileno": "h_crt__fileno",
    "api-ms-win-crt-stdio-l1-1-0.dll!fopen": "h_crt_fopen",
    "api-ms-win-crt-stdio-l1-1-0.dll!fclose": "h_crt_fclose",
    "api-ms-win-crt-stdio-l1-1-0.dll!fflush": "h_crt_fflush",
    "api-ms-win-crt-stdio-l1-1-0.dll!_get_osfhandle": "h_crt__get_osfhandle",
    "api-ms-win-crt-stdio-l1-1-0.dll!setvbuf": "h_crt_setvbuf",

    # CRT time
    "api-ms-win-crt-time-l1-1-0.dll!strftime": "h_crt_strftime",
    "api-ms-win-crt-time-l1-1-0.dll!_gmtime64": "h_crt__gmtime64",
    "api-ms-win-crt-time-l1-1-0.dll!_time64": "h_crt__time64",
    "api-ms-win-crt-time-l1-1-0.dll!_mkgmtime64": "h_crt__mkgmtime64",
    "api-ms-win-crt-time-l1-1-0.dll!_localtime64": "h_crt__localtime64",

    # CRT utility
    "api-ms-win-crt-utility-l1-1-0.dll!qsort": "h_crt_qsort",

    # bcrypt
    "bcrypt.dll!BCryptGenRandom": "h_bcrypt_stub",

    # dbghelp
    "dbghelp.dll!SymGetLineFromAddr": "h_dbghelp_stub",
    "dbghelp.dll!SymFromAddr": "h_dbghelp_stub",
    "dbghelp.dll!SymSetOptions": "h_dbghelp_stub",
    "dbghelp.dll!SymInitialize": "h_dbghelp_stub",

    # ole32
    "ole32.dll!CoInitializeEx": "h_ole32_stub",
    "ole32.dll!CoInitialize": "h_ole32_stub",
    "ole32.dll!CoUninitialize": "h_ole32_stub",

    # steam_api
    "steam_api.dll!SteamInternal_ContextInit": "h_steam_stub",
    "steam_api.dll!SteamAPI_GetHSteamPipe": "h_steam_stub",
    "steam_api.dll!SteamAPI_GetHSteamUser": "h_steam_stub",
    "steam_api.dll!SteamInternal_CreateInterface": "h_steam_stub",
    "steam_api.dll!SteamAPI_RegisterCallback": "h_steam_stub",
    "steam_api.dll!SteamAPI_RegisterCallResult": "h_steam_stub",
    "steam_api.dll!SteamAPI_RunCallbacks": "h_steam_stub",
    "steam_api.dll!SteamAPI_Shutdown": "h_steam_stub",
    "steam_api.dll!SteamAPI_UnregisterCallResult": "h_steam_stub",
    "steam_api.dll!SteamAPI_UnregisterCallback": "h_steam_stub",
    "steam_api.dll!SteamAPI_Init": "h_steam_stub",

    # MSVCP140 stream/ios internals. The game parses its data files through
    # std::istream; these are the pieces the DLL normally provides, so they get
    # real implementations instead of the blanket stub. Stack cleanup is NOT
    # described here: msvcp_ret_table.h pins the real `ret N` for every one of
    # them, because a wrong pop drifts the guest esp (see host_ohos.c).
    "MSVCP140.dll!??0?$basic_ios@DU?$char_traits@D@std@@@std@@IAE@XZ": "h_msvcp_ios_ctor",
    "MSVCP140.dll!??0?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAE@XZ": "h_msvcp_streambuf_ctor",
    "MSVCP140.dll!?_Ipfx@?$basic_istream@DU?$char_traits@D@std@@@std@@QAE_N_N@Z": "h_msvcp_ipfx",
    "MSVCP140.dll!?setstate@?$basic_ios@DU?$char_traits@D@std@@@std@@QAEXH_N@Z": "h_msvcp_setstate",
    "MSVCP140.dll!?sgetc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHXZ": "h_msvcp_sgetc",
    "MSVCP140.dll!?sbumpc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHXZ": "h_msvcp_sbumpc",
    "MSVCP140.dll!?snextc@?$basic_streambuf@DU?$char_traits@D@std@@@std@@QAEHXZ": "h_msvcp_snextc",
}

# Stubs by DLL prefix — for imports not individually assigned
DLL_STUBS = {
    "Lua5.3.3r.dll": "h_lua_stub",
    "OpenAL32.dll": "h_openal_stub",
    "MSVCP140.dll": "h_msvcp_stub",
    "VCRUNTIME140.dll": "h_vcruntime_stub",
}

imports = read_imports(EXE)

entries = []
for slot_rva, display in imports:
    if display in HANDLERS:
        handler = HANDLERS[display]
    else:
        dll = display.split("!")[0]
        handler = DLL_STUBS.get(dll, "hDUMMY")
    entries.append((display, handler))

# Sort by name for binary search
entries.sort(key=lambda x: x[0])

with open(OUTPUT, "w") as out:
    out.write("/* GENERATED sorted host import dispatch table.\n")
    out.write(" * Binary-searched by guest_host_import() in host_ohos.c.\n")
    out.write(" * Every PE import has an entry; unimplemented ones use hDUMMY. */\n")
    out.write("#ifndef HOST_IMPORTS_TABLE_H\n")
    out.write("#define HOST_IMPORTS_TABLE_H\n\n")
    out.write("static const host_import_entry s_host_imports[] = {\n")
    for display, handler in entries:
        out.write('    { %s, %s },\n' % (json.dumps(display, ensure_ascii=True), handler))
    out.write("};\n\n")
    out.write("#define HOST_IMPORT_COUNT %dU\n" % len(entries))
    out.write("#endif\n")

print("Generated %s" % OUTPUT)
print("  %d entries (sorted)" % len(entries))

# Verify sorting
is_sorted = all(entries[i][0] < entries[i+1][0] for i in range(len(entries)-1))
print("  Sorted: %s" % is_sorted)

# Count handlers
from collections import Counter
handler_counts = Counter(h for _, h in entries)
dummy_count = handler_counts.get("hDUMMY", 0)
print("  hDUMMY (unimplemented): %d" % dummy_count)
print("  Implemented: %d" % (len(entries) - dummy_count))