// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/http.h"

#include <mutex>

#include <cstdio>

#include "core/log.h"
#include "version.h"

#if defined(__PROSPERO__)

extern "C" {
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetPoolDestroy(int pool);
int sceSslInit(size_t poolSize);
int sceSslTerm(int context);
int sceHttpInit(int netPool, int sslContext, size_t poolSize);
int sceHttpTerm(int context);
int sceHttpCreateTemplate(int context, const char *userAgent, int version, int autoProxy);
int sceHttpDeleteTemplate(int id);
int sceHttpCreateConnectionWithURL(int tmpl, const char *url, int keepAlive);
int sceHttpDeleteConnection(int id);
int sceHttpCreateRequestWithURL(int connection, int method, const char *url, uint64_t contentLength);
int sceHttpDeleteRequest(int id);
int sceHttpSetAutoRedirect(int id, int enabled);
int sceHttpSetResolveTimeOut(int id, uint32_t usec);
int sceHttpSetConnectTimeOut(int id, uint32_t usec);
int sceHttpSetSendTimeOut(int id, uint32_t usec);
int sceHttpSetRecvTimeOut(int id, uint32_t usec);
int sceHttpsEnableOption(int id, uint32_t flags);
int sceHttpSendRequest(int request, const void *data, size_t size);
int sceHttpGetStatusCode(int request, int *status);
int sceHttpGetResponseContentLength(int request, int *result, uint64_t *length);
int sceHttpReadData(int request, void *data, size_t size);
}

namespace net {
namespace {
// Valeurs reprises de l'exemple du boilerplate, validé sur console.
constexpr int kNetPool = 1024 * 1024, kSslPool = 304 * 1024, kHttpPool = 4 * 1024 * 1024;
constexpr uint32_t kTimeout = 10000000;
constexpr uint32_t kVerify = 0x01 | 0x04 | 0x08 | 0x10 | 0x20 | 0x80;
int g_pool = -1, g_ssl = -1, g_http = -1, g_tmpl = -1;
}  // namespace

bool init() {
    static std::mutex initMutex;  // plusieurs threads (miroir, piste d'intro) peuvent arriver ensemble
    std::lock_guard<std::mutex> lock(initMutex);
    if (g_tmpl >= 0) return true;
    g_pool = sceNetPoolCreate("rhythm_net", kNetPool, 0);
    if (g_pool < 0) {
        LOG("http: sceNetPoolCreate = 0x%08x", static_cast<unsigned>(g_pool));
        return false;
    }
    g_ssl = sceSslInit(kSslPool);
    if (g_ssl < 0) {
        LOG("http: sceSslInit = 0x%08x", static_cast<unsigned>(g_ssl));
        return false;
    }
    g_http = sceHttpInit(g_pool, g_ssl, kHttpPool);
    if (g_http < 0) {
        LOG("http: sceHttpInit = 0x%08x", static_cast<unsigned>(g_http));
        return false;
    }
    g_tmpl = sceHttpCreateTemplate(g_http, RHYTHM_APP_NAME "/" RHYTHM_VERSION, 2, 0);
    if (g_tmpl < 0) {
        LOG("http: sceHttpCreateTemplate = 0x%08x", static_cast<unsigned>(g_tmpl));
        return false;
    }
    sceHttpSetAutoRedirect(g_tmpl, 1);
    sceHttpSetResolveTimeOut(g_tmpl, kTimeout);
    sceHttpSetConnectTimeOut(g_tmpl, kTimeout);
    sceHttpSetSendTimeOut(g_tmpl, kTimeout);
    sceHttpSetRecvTimeOut(g_tmpl, kTimeout);
    sceHttpsEnableOption(g_tmpl, kVerify);
    LOG("http: sceHttp pret");
    return true;
}

void shutdown() {
    if (g_tmpl >= 0) sceHttpDeleteTemplate(g_tmpl);
    if (g_http >= 0) sceHttpTerm(g_http);
    if (g_ssl >= 0) sceSslTerm(g_ssl);
    if (g_pool >= 0) sceNetPoolDestroy(g_pool);
    g_tmpl = g_http = g_ssl = g_pool = -1;
}

Response get(const std::string &url, size_t maxBytes, const Progress &progress, const std::atomic<bool> *cancel,
             const Sink &sink) {
    Response r;
    if (!init()) {
        r.error = "reseau indisponible";
        return r;
    }
    const int conn = sceHttpCreateConnectionWithURL(g_tmpl, url.c_str(), 1);
    if (conn < 0) {
        char e[64];
        snprintf(e, sizeof(e), "connexion 0x%08x", static_cast<unsigned>(conn));
        r.error = e;
        return r;
    }
    const int req = sceHttpCreateRequestWithURL(conn, 0, url.c_str(), 0);
    int rc = req;
    if (req >= 0) rc = sceHttpSendRequest(req, nullptr, 0);
    if (rc >= 0) rc = sceHttpGetStatusCode(req, &r.status);
    uint64_t total = 0;
    if (rc >= 0) {
        int has = 0;
        if (sceHttpGetResponseContentLength(req, &has, &total) < 0 || has != 0) total = 0;
    }
    uint64_t received = 0;
    if (rc >= 0) {
        static thread_local uint8_t buf[65536];
        for (;;) {
            if (cancel && cancel->load()) {
                r.error = "annule";
                break;
            }
            const int n = sceHttpReadData(req, buf, sizeof(buf));
            if (n < 0) {
                rc = n;
                break;
            }
            if (n == 0) break;
            received += static_cast<uint64_t>(n);
            if (received > maxBytes) {
                r.error = "reponse trop grande";
                break;
            }
            if (sink) {
                if (!sink(buf, static_cast<size_t>(n))) {
                    r.error = "ecriture impossible";
                    break;
                }
            } else {
                r.body.insert(r.body.end(), buf, buf + n);
            }
            if (progress) progress(received, total);
        }
    }
    if (rc < 0 && r.error.empty()) {
        char e[64];
        snprintf(e, sizeof(e), "sceHttp 0x%08x", static_cast<unsigned>(rc));
        r.error = e;
    }
    if (req >= 0) sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    return r;
}

}  // namespace net

#else  // PC : libcurl

#include <curl/curl.h>

namespace net {
namespace {
struct Ctx {
    Response *r;
    size_t max;
    const Progress *progress;
    const std::atomic<bool> *cancel;
    const Sink *sink;
    size_t received;
};
size_t onData(char *ptr, size_t size, size_t n, void *user) {
    Ctx *c = static_cast<Ctx *>(user);
    const size_t len = size * n;
    c->received += len;
    if (c->received > c->max) return 0;
    if (*c->sink) return (*c->sink)(reinterpret_cast<const uint8_t *>(ptr), len) ? len : 0;
    c->r->body.insert(c->r->body.end(), ptr, ptr + len);
    return len;
}
int onProgress(void *user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    Ctx *c = static_cast<Ctx *>(user);
    if (c->cancel && c->cancel->load()) return 1;
    if (*c->progress) (*c->progress)(static_cast<uint64_t>(now), static_cast<uint64_t>(total));
    return 0;
}
}  // namespace

bool init() {
    static bool done = curl_global_init(CURL_GLOBAL_DEFAULT) == 0;
    return done;
}
void shutdown() {}

Response get(const std::string &url, size_t maxBytes, const Progress &progress, const std::atomic<bool> *cancel,
             const Sink &sink) {
    Response r;
    init();
    CURL *curl = curl_easy_init();
    if (!curl) {
        r.error = "curl";
        return r;
    }
    Ctx ctx{&r, maxBytes, &progress, cancel, &sink, 0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
#if defined(_WIN32)
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));  // certificats de Windows
#endif
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, RHYTHM_APP_NAME "/" RHYTHM_VERSION);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    r.status = static_cast<int>(status);
    if (rc != CURLE_OK) r.error = cancel && cancel->load() ? "annule" : curl_easy_strerror(rc);
    curl_easy_cleanup(curl);
    return r;
}

}  // namespace net

#endif

namespace net {

std::string urlEncode(const std::string &s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

}  // namespace net
