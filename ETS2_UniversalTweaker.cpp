// ETS2_UniversalTweaker.cpp
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <mmsystem.h> // Untuk timeBeginPeriod
#pragma comment(lib, "winmm.lib")

DWORD GetMainThreadId(DWORD pid) {
    THREADENTRY32 te32;
    te32.dwSize = sizeof(THREADENTRY32);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

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
                        if (time < minTime) { minTime = time; mainThreadId = te32.th32ThreadID; }
                    }
                    CloseHandle(hThread);
                }
            }
        } while (Thread32Next(hSnap, &te32));
    }
    CloseHandle(hSnap);
    return mainThreadId;
}

void ApplyUniversalOptimizations() {
    // 1. Paksa Timer Resolution ke 1ms (Mengurangi input/render latency)
    timeBeginPeriod(1);

    // 2. Matikan Power Throttling Windows (Mencegah CPU clock turun saat micro-stutter)
    PROCESS_POWER_THROTTLING_STATE PowerThrottling;
    PowerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    PowerThrottling.StateMask = 0; // 0 = Disable
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling));

    // 3. Kunci Working Set RAM (Mencegah stuttering akibat OS memindahkan RAM game ke Pagefile)
    SIZE_T min, max;
    GetProcessWorkingSetSize(GetCurrentProcess(), &min, &max);
    SetProcessWorkingSetSize(GetCurrentProcess(), min + (128 * 1024 * 1024), max + (128 * 1024 * 1024));

    // 4. Naikkan Prioritas Process & Main Thread
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    
    DWORD mainTid = GetMainThreadId(GetCurrentProcessId());
    HANDLE hMain = OpenThread(THREAD_SET_INFORMATION, FALSE, mainTid);
    if (hMain) {
        SetThreadPriority(hMain, THREAD_PRIORITY_HIGHEST);
        CloseHandle(hMain);
    }

    // Log Indikator
    HANDLE hFile = CreateFileA("C:\\ets2_tweaker_log.txt", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char* msg = "[OK] Universal Tweaker Active: PowerThrottle OFF, RAM Locked, Timer 1ms, Priority HIGHEST.\n";
        DWORD bw; WriteFile(hFile, msg, (DWORD)strlen(msg), &bw, NULL);
        CloseHandle(hFile);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        ApplyUniversalOptimizations();
    }
    return TRUE;
}
