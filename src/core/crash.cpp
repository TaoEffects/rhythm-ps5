// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1  // REG_RIP sous Linux
#endif
#include "core/crash.h"

#if defined(_WIN32)
// Windows : pas de gestionnaire de plantage (signaux POSIX absents).
extern "C" void rhythm_crash_anchor() {}
namespace crash {
void install(const char *) {}
uintptr_t anchorAddress() { return reinterpret_cast<uintptr_t>(&rhythm_crash_anchor); }
}  // namespace crash
#else

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#if !defined(__APPLE__)
#include <ucontext.h>  // macOS : registres non lus, pas besoin
#endif
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "core/log.h"
#include "core/netlog.h"
#include "core/thread.h"

extern "C" __attribute__((noinline, used)) void rhythm_crash_anchor() { __asm__ volatile(""); }

namespace crash {
namespace {

int g_fd = -1;

pthread_t g_mainThread;
alignas(16) char g_altStack[64 * 1024];
bool g_viaLog = true;

// De préférence via le flux du log : le rapport arrive alors au PC dans le bon
// ordre, après les lignes qui le précèdent. Si le verrou du log est pris (plantage
// dans le log lui-même), on écrit directement partout.
void emit(const char *text) {
    if (g_viaLog && logx::tryWriteRaw(text)) return;
    g_viaLog = false;
    const size_t len = strlen(text);
    ssize_t ignored = write(1, text, len);
    if (g_fd >= 0) ignored = write(g_fd, text, len);
    const int client = netlog::clientFd();
    if (client >= 0) ignored = send(client, text, len, 0);
    (void)ignored;
}

uintptr_t relative(uintptr_t address) { return address - anchorAddress(); }

void handler(int sig, siginfo_t *info, void *context) {
    char line[160];
    uintptr_t rip = 0, rbp = 0, rsp = 0;
#if defined(__FreeBSD__)
    const ucontext_t *uc = static_cast<const ucontext_t *>(context);
    rip = static_cast<uintptr_t>(uc->uc_mcontext.mc_rip);
    rbp = static_cast<uintptr_t>(uc->uc_mcontext.mc_rbp);
    rsp = static_cast<uintptr_t>(uc->uc_mcontext.mc_rsp);
#elif defined(__linux__) && defined(__x86_64__)
    const ucontext_t *uc = static_cast<const ucontext_t *>(context);
    rip = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
    rbp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]);
    rsp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RSP]);
#else
    (void)context;
#endif
    snprintf(line, sizeof(line), "\n!!! PLANTAGE signal=%d adresse=%p rip=ancre%+lld\n", sig,
             info ? info->si_addr : nullptr, static_cast<long long>(relative(rip)));
    emit(line);
    uintptr_t low = 0, high = 0;
    thr::stackBounds(low, high);
    snprintf(line, sizeof(line), "!!! thread=%s rsp=%p rbp=%p pile=[%p, %p]\n",
             pthread_equal(pthread_self(), g_mainThread) ? "principal" : "secondaire", reinterpret_cast<void *>(rsp),
             reinterpret_cast<void *>(rbp), reinterpret_cast<void *>(low), reinterpret_cast<void *>(high));
    emit(line);
    // Pile d'appels via les pointeurs de cadre (compilé avec -fno-omit-frame-pointer).
    for (int depth = 0; depth < 24 && rbp != 0 && (rbp & 7u) == 0; ++depth) {
        const uintptr_t *frame = reinterpret_cast<const uintptr_t *>(rbp);
        const uintptr_t ret = frame[1];
        if (ret == 0) break;
        snprintf(line, sizeof(line), "!!!   #%02d ancre%+lld\n", depth, static_cast<long long>(relative(ret)));
        emit(line);
        const uintptr_t next = frame[0];
        if (next <= rbp) break;  // la pile croît vers le bas : le cadre parent est plus haut
        rbp = next;
    }
    emit("!!! fin du rapport\n");
    if (g_viaLog) usleep(400 * 1000);  // laisse le thread réseau envoyer le rapport
    _exit(128 + sig);
}

}  // namespace

uintptr_t anchorAddress() { return reinterpret_cast<uintptr_t>(&rhythm_crash_anchor); }

void install(const char *logFilePath) {
    if (logFilePath && *logFilePath) g_fd = open(logFilePath, O_WRONLY | O_APPEND);
    g_mainThread = pthread_self();
    // Pile dédiée au gestionnaire : il doit pouvoir tourner même si la pile du
    // thread fautif est épuisée.
    stack_t ss {};
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof(g_altStack);
    const int alt = sigaltstack(&ss, nullptr);
    struct sigaction sa {};
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | (alt == 0 ? SA_ONSTACK : 0);
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, nullptr);
}

}  // namespace crash
#endif
