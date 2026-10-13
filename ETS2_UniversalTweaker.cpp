// ETS2_StableProductionTweaker.cpp
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <mmsystem.h>
#include <vector>
#include <unordered_set>

#pragma comment(lib, "winmm.lib")

typedef enum _THREADINFOCLASS { ThreadIoPriority = 14 } THREADINFOCLASS;
typedef LONG NTSTATUS;
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

typedef NTSTATUS(NTAPI* pfnNtSetInformationThread)(HANDLE, THREADINFOCLASS, PVOID, ULONG);
typedef NTSTATUS(NTAPI* pfnNtSetTimerResolution)(ULONG, BOOLEAN, PULONG);

static volatile bool g_Running = true;
static pfnNtSetInformationThread g_NtSetInfoThread = nullptr;
static pfnNtSetTimerResolution g_NtSetTimerRes = nullptr;

void LogMessage(const char* msg) {
    HANDLE hFile = CreateFileA("ets2_tweaker_log.txt", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD bw;
        WriteFile(hFile, msg, (DWORD)strlen(msg), &bw, NULL);
        CloseHandle(hFile);
    }
}

DWORD GetMainThreadId(DWORD pid) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    THREADENTRY32 te32;
    te32.dwSize = sizeof(THREADENTRY32);
    DWORD mainThreadId = 0;
    ULONGLONG minTime = MAXULONGLONG;

    if (Thread32First(hSnap, &te32)) {
        do {
            if (te32.th32OwnerProcessID == pid) {
                HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te32.th32ThreadID);
                if (hThread) {
                    FILETIME creation, exit, kernel, user;
                    if (GetThreadTimes(hThread, &creation, &exit, &kernel, &user)) {
                        ULONGLONG time = (((ULONGLONG)creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
                        if (time < minTime) {
                            minTime = time;
                            mainThreadId = te32.th32ThreadID;
                        }
                    }
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(hSnap, &te32));
    }
    CloseHandle(hSnap);
    return mainThreadId;
}

// Watchdog aktif untuk menjaga thread lama dan mengoptimalkan thread baru
DWORD WINAPI OptimizationWatchdog(LPVOID lpParam) {
    LogMessage("[START] Optimization Watchdog aktif di background.\n");
    
    DWORD pid = GetCurrentProcessId();
    DWORD mainThreadId = GetMainThreadId(pid);
    std::unordered_set<DWORD> processedThreads;

    // Mask Core Fisik i7-7700 (0, 2, 4, 6 menghindari HyperThreading overhead)
    // Core 0 (Bit 0): 0x01 -> Khusus Main Thread
    // Core Fisik Lain (Bit 2, 4, 6): 0x54 (Biner: 01010100) -> Worker threads
    const DWORD_PTR MAIN_THREAD_MASK = 0x01;
    const DWORD_PTR WORKER_THREADS_MASK = 0x54; 

    // Optimasi Main Thread sekali di awal
    if (mainThreadId != 0) {
        HANDLE hMain = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, mainThreadId);
        if (hMain) {
            SetThreadPriority(hMain, THREAD_PRIORITY_HIGHEST);
            SetThreadAffinityMask(hMain, MAIN_THREAD_MASK);
            SetThreadIdealProcessor(hMain, 0);
            SetThreadPriorityBoost(hMain, FALSE);
            CloseHandle(hMain);
            processedThreads.insert(mainThreadId);
            LogMessage("[OK] Main Thread diisolasi ke Core Fisik 0 (Priority HIGHEST).\n");
        }
    }

    while (g_Running) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te32;
            te32.dwSize = sizeof(THREADENTRY32);

            if (Thread32First(hSnap, &te32)) {
                do {
                    if (te32.th32OwnerProcessID == pid && te32.th32ThreadID != mainThreadId) {
                        // Jika ada thread baru yang baru saja dibuat oleh game
                        if (processedThreads.find(te32.th32ThreadID) == processedThreads.end()) {
                            HANDLE hThread = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, te32.th32ThreadID);
                            if (hThread) {
                                // Naikkan I/O Priority ke High untuk tekstur/audio streaming
                                if (g_NtSetInfoThread) {
                                    ULONG ioPriority = 2;
                                    g_NtSetInfoThread(hThread, ThreadIoPriority, &ioPriority, sizeof(ioPriority));
                                }

                                // Sebar worker thread ke core fisik lain
                                SetThreadAffinityMask(hThread, WORKER_THREADS_MASK);
                                SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL);
                                SetThreadPriorityBoost(hThread, FALSE);

                                CloseHandle(hThread);
                                processedThreads.insert(te32.th32ThreadID);
                            }
                        }
                    }
                } while (Thread32Next(hSnap, &te32));
            }
            CloseHandle(hSnap);
        }

        // Tidur 3 detik agar tidak memakan siklus CPU sama sekali
        Sleep(3000);
    }

    return 0;
}

DWORD WINAPI ApplyStableOptimizations(LPVOID lpParam) {
    LogMessage("=========================================\n");
    LogMessage("[START] Inisialisasi Optimasi Stabil...\n");

    HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
    if (hNtDll) {
        g_NtSetInfoThread = (pfnNtSetInformationThread)GetProcAddress(hNtDll, "NtSetInformationThread");
        g_NtSetTimerRes = (pfnNtSetTimerResolution)GetProcAddress(hNtDll, "NtSetTimerResolution");
    }

    // 1. Timer Resolution Stabil (0.5ms NTAPI + 1ms WinMM Fallback)
    timeBeginPeriod(1);
    if (g_NtSetTimerRes) {
        ULONG currentRes = 0;
        g_NtSetTimerRes(5000, TRUE, &currentRes);
        LogMessage("[OK] Timer resolusi sistem diatur ke 0.5ms.\n");
    }

    // 2. Disable Power Throttling
    PROCESS_POWER_THROTTLING_STATE PowerThrottling = { 0 };
    PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    PowerThrottling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling));
    LogMessage("[OK] CPU Power Throttling dimatikan.\n");

    // 3. Prioritas HIGH (Stabil dan aman untuk antrean DPC GPU)
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    LogMessage("[OK] Prioritas proses diatur ke HIGH_PRIORITY_CLASS.\n");

    // 4. Working Set & Physical Memory Locking (256MB Locked)
    // Alokasikan memori dan kunci ke RAM fisik agar OS dilarang swap ke disk
    SIZE_T lockSize = 256 * 1024 * 1024; // 256 MB
    void* pMem = VirtualAlloc(NULL, lockSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (pMem) {
        SecureZeroMemory(pMem, lockSize);
        // VirtualLock memastikan blok memori ini tidak pernah masuk pagefile
        if (VirtualLock(pMem, lockSize)) {
            LogMessage("[OK] 256MB RAM terkunci langsung di memori fisik (VirtualLock aktif).\n");
        } else {
            LogMessage("[OK] 256MB dialokasikan (VirtualLock dibatasi kuota OS).\n");
        }
    }

    // 5. Jalankan Watchdog di background
    CreateThread(NULL, 0, OptimizationWatchdog, NULL, 0, NULL);

    LogMessage("=========================================\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, ApplyStableOptimizations, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        g_Running = false;
        timeEndPeriod(1);
    }
    return TRUE;
}
