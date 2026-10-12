// ETS2_ExtremeCoreForcer.cpp
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <mmsystem.h>
#include <vector>

#pragma comment(lib, "winmm.lib")

// NTAPI untuk I/O Priority
typedef enum _THREADINFOCLASS { ThreadIoPriority = 14 } THREADINFOCLASS;
typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* pfnNtSetInformationThread)(HANDLE, THREADINFOCLASS, PVOID, ULONG);

void LogMessage(const char* msg) {
    HANDLE hFile = CreateFileA("ets2_extreme_tweaker.txt", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD bw;
        WriteFile(hFile, msg, (DWORD)strlen(msg), &bw, NULL);
        CloseHandle(hFile);
    }
}

// Fungsi untuk mendapatkan Core Mask (i7-7700 memiliki 8 Logical Core)
// Core 0,1 (Physical 0) | Core 2,3 (Physical 1) | Core 4,5 (Physical 2) | Core 6,7 (Physical 3)
DWORD_PTR GetMaskForMainThread() {
    // Paksa Main Thread ke Physical Core 1 & 2 (Logical 2, 3, 4, 5) 
    // Menjauhkan Main Thread dari Core 0 yang sering dipakai OS Windows.
    return 0b00111100; 
}

DWORD_PTR GetMaskForWorkerThreads() {
    // Paksa thread pendukung (background) ke Physical Core 3 (Logical 6, 7) dan Core 0 (Logical 0, 1)
    return 0b11000011;
}

DWORD WINAPI ActiveCoreScheduler(LPVOID lpParam) {
    LogMessage("=========================================\n");
    LogMessage("[!!!] EXTREME CORE FORCER INITIALIZED [!!!]\n");

    // 1. Eksekusi Agresif: Matikan Power Throttling
    PROCESS_POWER_THROTTLING_STATE PowerThrottling = { PROCESS_POWER_THROTTLING_CURRENT_VERSION, PROCESS_POWER_THROTTLING_EXECUTION_SPEED, 0 };
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling));

    // 2. Eksekusi Agresif: Paksa Prioritas Proses ke REALTIME
    // Catatan Eksperimen: REALTIME memaksa CPU memproses game ini di atas segala hal, termasuk input mouse/keyboard OS.
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    LogMessage("[WARNING] Process Priority dipaksa ke REALTIME_PRIORITY_CLASS.\n");

    // 3. Paksa Proses untuk mengenali semua 8 Threads (Affinity Process)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD_PTR fullMask = (1ULL << sysInfo.dwNumberOfProcessors) - 1;
    SetProcessAffinityMask(GetCurrentProcess(), fullMask);

    HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
    pfnNtSetInformationThread NtSetInfoThread = hNtDll ? (pfnNtSetInformationThread)GetProcAddress(hNtDll, "NtSetInformationThread") : nullptr;

    DWORD pid = GetCurrentProcessId();

    // Loop Tanpa Henti: Memaksa thread baru yang muncul di game agar patuh pada aturan Core kita
    while (true) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te32;
            te32.dwSize = sizeof(THREADENTRY32);

            DWORD mainThreadId = 0;
            ULONGLONG minTime = MAXULONGLONG;
            std::vector<DWORD> allThreads;

            if (Thread32First(hSnap, &te32)) {
                do {
                    if (te32.th32OwnerProcessID == pid) {
                        allThreads.push_back(te32.th32ThreadID);
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

            // Eksekusi Pemisahan Core (Affinity Pinning)
            for (DWORD tid : allThreads) {
                HANDLE hThread = OpenThread(THREAD_SET_INFORMATION | THREAD_QUERY_INFORMATION, FALSE, tid);
                if (hThread) {
                    if (tid == mainThreadId) {
                        // MAIN THREAD: Paksa ke Core Tengah, Prioritas Tertinggi, I/O Kritis
                        SetThreadAffinityMask(hThread, GetMaskForMainThread());
                        SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL);
                        if (NtSetInfoThread) {
                            ULONG ioPriority = 3; // 3 = Critical
                            NtSetInfoThread(hThread, ThreadIoPriority, &ioPriority, sizeof(ioPriority));
                        }
                    } else {
                        // WORKER THREAD: Paksa ke sisa Core, Prioritas Tinggi
                        SetThreadAffinityMask(hThread, GetMaskForWorkerThreads());
                        SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);
                    }
                    CloseHandle(hThread);
                }
            }
        }
        
        // Tidur 2 detik, lalu scan dan paksa ulang jika game membuat thread baru saat loading map/aset
        Sleep(2000); 
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        timeBeginPeriod(1); // Force 1ms Timer
        
        // Memulai background scheduler yang akan memaksa CPU Core secara realtime
        CreateThread(NULL, 0, ActiveCoreScheduler, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        timeEndPeriod(1);
    }
    return TRUE;
}
