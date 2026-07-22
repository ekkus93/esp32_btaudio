/* url_policy — see url_policy.h. Needs inet_pton()/AF_INET(6)/getaddrinfo()
 * — on ESP-IDF these only come through fully via lwip's own headers
 * (plain <arpa/inet.h> resolves to a slimmer header that doesn't declare
 * inet_pton, which then collides with lwip/sockets.h's later `static
 * inline` definition once <netdb.h> pulls it in transitively). On host,
 * plain POSIX headers provide everything directly. */
#include "url_policy.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#ifdef CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS
#define LOCAL_STREAMS_ALLOWED 1
#else
#define LOCAL_STREAMS_ALLOWED 0
#endif

static bool ipv4_octets_allowed(uint8_t o1, uint8_t o2)
{
    if (LOCAL_STREAMS_ALLOWED) return true;
    if (o1 == 0) return false;              /* unspecified/this-network 0.0.0.0/8 */
    if (o1 == 10) return false;              /* private 10/8 */
    if (o1 == 127) return false;             /* loopback 127/8 */
    if (o1 == 169 && o2 == 254) return false; /* link-local 169.254/16 */
    if (o1 == 172 && o2 >= 16 && o2 <= 31) return false; /* private 172.16/12 */
    if (o1 == 192 && o2 == 168) return false; /* private 192.168/16 */
    if (o1 >= 224) return false;              /* multicast 224/4 + broadcast 255.255.255.255 */
    return true;
}

bool url_policy_ipv4_allowed(uint32_t addr_be)
{
    uint32_t a = ntohl(addr_be);
    return ipv4_octets_allowed((uint8_t)(a >> 24), (uint8_t)(a >> 16));
}

bool url_policy_ipv6_allowed(const uint8_t a[16])
{
    if (LOCAL_STREAMS_ALLOWED) return true;

    bool all_zero = true;
    for (int i = 0; i < 16; ++i) {
        if (a[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) return false;   /* unspecified :: */

    bool loopback_prefix = true;
    for (int i = 0; i < 15; ++i) {
        if (a[i] != 0) { loopback_prefix = false; break; }
    }
    if (loopback_prefix && a[15] == 1) return false;   /* loopback ::1 */

    if (a[0] == 0xFF) return false;                    /* multicast ff00::/8 */
    if ((a[0] & 0xFE) == 0xFC) return false;            /* unique-local fc00::/7 */
    if (a[0] == 0xFE && (a[1] & 0xC0) == 0x80) return false; /* link-local fe80::/10 */

    /* IPv4-mapped ::ffff:a.b.c.d — bytes 0..9 zero, 10..11 = 0xFF,0xFF. */
    bool v4_mapped_prefix = true;
    for (int i = 0; i < 10; ++i) {
        if (a[i] != 0) { v4_mapped_prefix = false; break; }
    }
    if (v4_mapped_prefix && a[10] == 0xFF && a[11] == 0xFF) {
        return ipv4_octets_allowed(a[12], a[13]);
    }

    return true;
}

bool url_policy_parse_literal_ipv4(const char *host, uint32_t *out_addr_be)
{
    if (!host || !out_addr_be) return false;
    struct in_addr addr;
    if (inet_pton(AF_INET, host, &addr) != 1) return false;
    *out_addr_be = addr.s_addr;
    return true;
}

bool url_policy_parse_literal_ipv6(const char *host, uint8_t out_addr[16])
{
    if (!host || !out_addr) return false;
    struct in6_addr addr;
    if (inet_pton(AF_INET6, host, &addr) != 1) return false;
    memcpy(out_addr, &addr, 16);
    return true;
}

bool url_policy_extract_host(const char *url, char *host_out, size_t host_out_sz)
{
    if (!url || !host_out || host_out_sz == 0) return false;
    const char *p = strstr(url, "://");
    if (!p) return false;
    p += 3;

    /* IPv6 literal in brackets: [::1]:port/path */
    const char *start = p;
    const char *end;
    if (*p == '[') {
        end = strchr(p, ']');
        if (!end) return false;
        start = p + 1;
    } else {
        end = p;
        while (*end && *end != '/' && *end != ':' && *end != '?' && *end != '#') end++;
    }

    size_t len = (size_t)(end - start);
    if (len == 0 || len >= host_out_sz) return false;
    memcpy(host_out, start, len);
    host_out[len] = '\0';
    return true;
}

bool url_policy_check_literal(const char *url)
{
    char host[256];
    if (!url_policy_extract_host(url, host, sizeof(host))) {
        return false;   /* malformed enough that we can't even find a host */
    }

    uint32_t v4;
    if (url_policy_parse_literal_ipv4(host, &v4)) {
        return url_policy_ipv4_allowed(v4);
    }
    uint8_t v6[16];
    if (url_policy_parse_literal_ipv6(host, v6)) {
        return url_policy_ipv6_allowed(v6);
    }
    /* Not a literal IP — it's a hostname; DNS-time policy is device-only
     * (url_policy_resolve_and_check(), wired in Phase 8). */
    return true;
}

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t url_policy_resolve_and_check(const char *host, const char *port)
{
    if (!host) return ESP_ERR_INVALID_ARG;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    struct addrinfo *results = NULL;
    int rc = getaddrinfo(host, (port && *port) ? port : "80", &hints, &results);
    if (rc != 0 || !results) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ESP_OK;
    for (const struct addrinfo *it = results; it; it = it->ai_next) {
        bool allowed = true;
        if (it->ai_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)(void *)it->ai_addr;
            allowed = url_policy_ipv4_allowed(sin->sin_addr.s_addr);
        } else if (it->ai_family == AF_INET6) {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)(void *)it->ai_addr;
            allowed = url_policy_ipv6_allowed(sin6->sin6_addr.s6_addr);
        }
        if (!allowed) {
            /* Reject rather than silently pick a different result —
             * closes DNS-rebinding ambiguity (FIX3 §8.6). */
            err = ESP_ERR_INVALID_ARG;
            break;
        }
    }
    freeaddrinfo(results);
    return err;
}
#endif
