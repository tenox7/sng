#include "os_interface.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ipexport.h>
#include <icmpapi.h>
#include <pdh.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t ft_to_u64(const FILETIME *ft) {
    return ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
}

struct plot_mutex_t {
    CRITICAL_SECTION cs;
};

struct plot_thread_t {
    HANDLE handle;
    DWORD thread_id;
};

const char* os_get_platform_name(void) {
    return "win32";
}

int os_init(void) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    return 1;
}

void os_cleanup(void) {
    WSACleanup();
}

int os_cpu_get_stats(double *value) {
    static uint64_t prev_idle = 0, prev_total = 0;
    FILETIME idle_ft, kernel_ft, user_ft;
    uint64_t idle, kernel, user, total;
    uint64_t idle_diff, total_diff;

    if (!value) return 0;
    if (!GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) return 0;

    idle = ft_to_u64(&idle_ft);
    kernel = ft_to_u64(&kernel_ft);
    user = ft_to_u64(&user_ft);
    total = kernel + user;

    if (prev_total != 0) {
        idle_diff = idle - prev_idle;
        total_diff = total - prev_total;
        if (total_diff > 0) {
            *value = 100.0 * (1.0 - (double)idle_diff / (double)total_diff);
        } else {
            *value = 0.0;
        }
    } else {
        *value = 0.0;
    }

    prev_idle = idle;
    prev_total = total;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;
    return 1;
}

int os_cpu_get_stats_dual(double *total_value, double *system_value) {
    static uint64_t prev_idle = 0, prev_kernel = 0, prev_total = 0;
    FILETIME idle_ft, kernel_ft, user_ft;
    uint64_t idle, kernel, user, total;
    uint64_t idle_diff, total_diff, sys_diff;

    if (!total_value || !system_value) return 0;
    if (!GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) return 0;

    idle = ft_to_u64(&idle_ft);
    kernel = ft_to_u64(&kernel_ft);
    user = ft_to_u64(&user_ft);
    total = kernel + user;

    if (prev_total != 0) {
        idle_diff = idle - prev_idle;
        total_diff = total - prev_total;
        sys_diff = (kernel - prev_kernel) - idle_diff;
        if (total_diff > 0) {
            *total_value = 100.0 * (1.0 - (double)idle_diff / (double)total_diff);
            *system_value = 100.0 * (double)sys_diff / (double)total_diff;
        } else {
            *total_value = 0.0;
            *system_value = 0.0;
        }
    } else {
        *total_value = 0.0;
        *system_value = 0.0;
    }

    prev_idle = idle;
    prev_kernel = kernel;
    prev_total = total;

    if (*total_value > 100.0) *total_value = 100.0;
    if (*total_value < 0.0) *total_value = 0.0;
    if (*system_value > 100.0) *system_value = 100.0;
    if (*system_value < 0.0) *system_value = 0.0;
    return 1;
}

int os_memory_get_stats(double *value) {
    MEMORYSTATUSEX ms;

    if (!value) return 0;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 0;

    if (ms.ullTotalPhys == 0) return 0;
    *value = 100.0 * (double)(ms.ullTotalPhys - ms.ullAvailPhys) / (double)ms.ullTotalPhys;

    if (*value > 100.0) *value = 100.0;
    if (*value < 0.0) *value = 0.0;
    return 1;
}

int os_loadavg_get_stats(double *value) {
    static PDH_HQUERY query = NULL;
    static PDH_HCOUNTER counter = NULL;
    PDH_FMT_COUNTERVALUE val;

    if (!value) return 0;

    if (!query) {
        if (PdhOpenQueryA(NULL, 0, &query) != ERROR_SUCCESS) {
            query = NULL;
            return 0;
        }
        if (PdhAddEnglishCounterA(query, "\\System\\Processor Queue Length", 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = NULL;
            return 0;
        }
        PdhCollectQueryData(query);
    }

    if (PdhCollectQueryData(query) != ERROR_SUCCESS) return 0;
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &val) != ERROR_SUCCESS) return 0;

    *value = val.doubleValue;
    return 1;
}

int os_get_interface_stats(const char* interface_name, uint32_t* in_bytes, uint32_t* out_bytes) {
    PMIB_IFTABLE table = NULL;
    DWORD size = 0;
    DWORD i;
    int found = 0;

    if (!interface_name || !in_bytes || !out_bytes) return 0;

    if (GetIfTable(NULL, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER) return 0;
    table = (PMIB_IFTABLE)malloc(size);
    if (!table) return 0;
    if (GetIfTable(table, &size, FALSE) != NO_ERROR) { free(table); return 0; }

    for (i = 0; i < table->dwNumEntries; i++) {
        MIB_IFROW *row = &table->table[i];
        if (row->dwType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (row->dwOperStatus != IF_OPER_STATUS_OPERATIONAL &&
            row->dwOperStatus != IF_OPER_STATUS_CONNECTED) continue;

        if (strcmp(interface_name, "any") == 0 ||
            strstr((const char*)row->bDescr, interface_name) != NULL) {
            *in_bytes = row->dwInOctets;
            *out_bytes = row->dwOutOctets;
            found = 1;
            break;
        }
    }

    free(table);
    return found;
}

void os_sleep(uint32_t milliseconds) {
    Sleep((DWORD)milliseconds);
}

uint32_t os_get_time_ms(void) {
    return (uint32_t)GetTickCount();
}

plot_mutex_t *os_plot_mutex_create(void) {
    plot_mutex_t *mutex;

    mutex = (plot_mutex_t*)malloc(sizeof(plot_mutex_t));
    if (!mutex) return NULL;

    InitializeCriticalSection(&mutex->cs);
    return mutex;
}

void os_plot_mutex_destroy(plot_mutex_t *mutex) {
    if (!mutex) return;
    DeleteCriticalSection(&mutex->cs);
    free(mutex);
}

void os_plot_mutex_lock(plot_mutex_t *mutex) {
    if (!mutex) return;
    EnterCriticalSection(&mutex->cs);
}

void os_plot_mutex_unlock(plot_mutex_t *mutex) {
    if (!mutex) return;
    LeaveCriticalSection(&mutex->cs);
}

plot_thread_t *os_plot_thread_create(void (*func)(void *), void *arg) {
    plot_thread_t *thread;

    thread = (plot_thread_t*)malloc(sizeof(plot_thread_t));
    if (!thread) return NULL;

    thread->handle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, &thread->thread_id);
    if (!thread->handle) {
        free(thread);
        return NULL;
    }

    return thread;
}

void os_plot_thread_destroy(plot_thread_t *thread) {
    if (!thread) return;
    if (thread->handle) CloseHandle(thread->handle);
    free(thread);
}

void os_plot_thread_join(plot_thread_t *thread) {
    if (!thread || !thread->handle) return;
    WaitForSingleObject(thread->handle, INFINITE);
}

int os_plot_thread_join_timeout(plot_thread_t *thread, uint32_t timeout_ms) {
    DWORD result;

    if (!thread || !thread->handle) return 0;

    result = WaitForSingleObject(thread->handle, (DWORD)timeout_ms);
    return (result == WAIT_OBJECT_0);
}

char *os_get_config_path(const char *filename) {
    static char config_path[512];
    char *appdata;

    appdata = getenv("APPDATA");
    if (appdata) {
        sprintf(config_path, "%s\\%s", appdata, filename);
    } else {
        sprintf(config_path, "%s", filename);
    }

    return config_path;
}

struct os_ping_context_t {
    HANDLE icmp;
    IPAddr dest;
    DWORD timeout_ms;
};

os_ping_context_t *os_ping_create(const char *hostname, uint32_t timeout_ms) {
    os_ping_context_t *ctx;
    WSADATA wsaData;
    struct hostent *host;

    WSAStartup(MAKEWORD(1, 1), &wsaData);

    host = gethostbyname(hostname);
    if (!host) {
        WSACleanup();
        return NULL;
    }

    ctx = (os_ping_context_t*)malloc(sizeof(os_ping_context_t));
    if (!ctx) {
        WSACleanup();
        return NULL;
    }

    ctx->icmp = IcmpCreateFile();
    if (ctx->icmp == INVALID_HANDLE_VALUE) {
        free(ctx);
        WSACleanup();
        return NULL;
    }

    memcpy(&ctx->dest, host->h_addr, 4);
    ctx->timeout_ms = (DWORD)timeout_ms;

    return ctx;
}

int os_ping_send(os_ping_context_t *ctx, double *ping_time_ms) {
    char send_data[32];
    char reply_buf[sizeof(ICMP_ECHO_REPLY) + 32 + 8];
    DWORD n;
    ICMP_ECHO_REPLY *reply;

    if (!ctx || !ping_time_ms) return 0;

    memset(send_data, 0, sizeof(send_data));
    n = IcmpSendEcho(ctx->icmp, ctx->dest, send_data, sizeof(send_data),
                     NULL, reply_buf, sizeof(reply_buf), ctx->timeout_ms);
    if (n == 0) return 0;

    reply = (ICMP_ECHO_REPLY*)reply_buf;
    if (reply->Status != IP_SUCCESS) return 0;

    *ping_time_ms = (double)reply->RoundTripTime;
    return 1;
}

void os_ping_destroy(os_ping_context_t *ctx) {
    if (!ctx) return;
    if (ctx->icmp != INVALID_HANDLE_VALUE) {
        IcmpCloseHandle(ctx->icmp);
    }
    free(ctx);
    WSACleanup();
}
