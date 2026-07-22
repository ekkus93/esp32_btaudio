/*
 * url_policy — stream-destination SSRF policy (FIX3 §8.6 / URL-001).
 *
 * Pure address-range checks (host-testable, no ESP-IDF deps): reject
 * loopback/link-local/private/multicast/unspecified/broadcast IPv4 and
 * IPv6 destinations by default. CONFIG_ESP_I2S_SOURCE_ALLOW_LOCAL_STREAMS
 * (default n) disables the whole policy for local development.
 *
 * DNS resolution (url_policy_resolve_and_check()) is device-only —
 * getaddrinfo() isn't meaningfully host-testable — and is wired into the
 * radio HTTP client's initial connect, redirect-follow, and reconnect
 * paths in Phase 8, which already owns that client's internals.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* addr_be is a raw IPv4 address in network byte order (e.g. from
 * struct in_addr.s_addr or a parsed literal). */
bool url_policy_ipv4_allowed(uint32_t addr_be);

/* a[16] is a raw IPv6 address in network byte order. Also rejects
 * IPv4-mapped IPv6 addresses (::ffff:a.b.c.d) whose embedded IPv4 is
 * itself disallowed. */
bool url_policy_ipv6_allowed(const uint8_t a[16]);

/* Parse `host` as a literal IPv4/IPv6 address (no DNS). Returns true and
 * fills *out_addr_be on success; false if host is not a literal IPv4
 * address (e.g. a hostname, or a v6 literal — try the v6 parser). */
bool url_policy_parse_literal_ipv4(const char *host, uint32_t *out_addr_be);
bool url_policy_parse_literal_ipv6(const char *host, uint8_t out_addr[16]);

/* Extract the host substring from a "scheme://host[:port][/path]" URL
 * into host_out (NUL-terminated, truncated to fit host_out_sz). Returns
 * false if no "://" separator is found. Pure syntax only — does not
 * validate the scheme. */
bool url_policy_extract_host(const char *url, char *host_out, size_t host_out_sz);

/* Full literal-destination check for a URL: extract the host, and if it
 * parses as a literal IPv4 or IPv6 address, apply the range policy.
 * Returns true when the host is NOT a literal (a hostname — DNS
 * resolution happens separately, device-side) or is a literal that
 * passes policy. Returns false only for a literal that is blocked. */
bool url_policy_check_literal(const char *url);

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* Resolve `host` via DNS and check every returned address against policy.
 * Returns ESP_OK if at least one result exists and ALL results pass
 * policy (DNS-rebinding-safe: never silently pick a different result),
 * ESP_ERR_NOT_FOUND if resolution failed, ESP_ERR_INVALID_ARG if any
 * result is blocked. `port` may be NULL/empty for a default. */
esp_err_t url_policy_resolve_and_check(const char *host, const char *port);
#endif

#ifdef __cplusplus
}
#endif
