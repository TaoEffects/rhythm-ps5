// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1  // pthread_getattr_np sous Linux
#endif
#include <cstdint>

#include "core/thread.h"

#if defined(__FreeBSD__)
#include <pthread_np.h>
#endif

#include "core/log.h"

namespace thr {

bool start(pthread_t &out, void *(*fn)(void *), void *arg, const char *name, size_t stackBytes) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) return false;
    pthread_attr_setstacksize(&attr, stackBytes);
    const int r = pthread_create(&out, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    LOG("thread: %s cree (pile %zu Ko) -> %d", name, stackBytes / 1024, r);
    return r == 0;
}

void stackBounds(uintptr_t &low, uintptr_t &high) {
    low = high = 0;
#if defined(_WIN32)
    return;  // Windows : non mesuré (journal seulement)
#elif defined(__APPLE__)
    // macOS : la pile se lit directement.
    high = reinterpret_cast<uintptr_t>(pthread_get_stackaddr_np(pthread_self()));
    low = high - pthread_get_stacksize_np(pthread_self());
#else
    pthread_attr_t attr;
#if defined(__FreeBSD__)
    if (pthread_attr_init(&attr) != 0) return;
    if (pthread_attr_get_np(pthread_self(), &attr) == 0) {
#else
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
#endif
        void *addr = nullptr;
        size_t size = 0;
        if (pthread_attr_getstack(&attr, &addr, &size) == 0) {
            low = reinterpret_cast<uintptr_t>(addr);
            high = low + size;
        }
    }
    pthread_attr_destroy(&attr);
#endif
}

void logStackInfo(const char *who) {
    uintptr_t low = 0, high = 0;
    stackBounds(low, high);
    int marker = 0;
    const uintptr_t sp = reinterpret_cast<uintptr_t>(&marker);
    LOG("pile %s: sp=%p bornes=[%p, %p] taille=%zu Ko marge=%zu Ko", who, reinterpret_cast<void *>(sp),
        reinterpret_cast<void *>(low), reinterpret_cast<void *>(high), static_cast<size_t>((high - low) / 1024),
        static_cast<size_t>(sp > low ? (sp - low) / 1024 : 0));
}

}  // namespace thr
