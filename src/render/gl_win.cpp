// SPDX-License-Identifier: GPL-3.0-or-later
// Chargement des fonctions OpenGL au-delà de 1.1 sous Windows.
#if defined(_WIN32)
#include "render/gl.h"

#include "core/log.h"

#define X(type, name) type rhythm_##name = nullptr;
RHYTHM_GL_LOADED(X)
#undef X

namespace gl {

bool loadFunctions(void *(*getProc)(const char *)) {
    bool ok = true;
#define X(type, name)                                                     \
    rhythm_##name = reinterpret_cast<type>(getProc(#name));               \
    if (!rhythm_##name) {                                                 \
        LOG("gl: fonction %s introuvable", #name);                        \
        ok = false;                                                       \
    }
    RHYTHM_GL_LOADED(X)
#undef X
    return ok;
}

}  // namespace gl
#endif
