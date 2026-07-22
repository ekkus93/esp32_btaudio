/*
 * stations_persist_core — pure, host-testable station-blob logic (FIX3
 * Phase 5A): CRC-32, V2 blob construction/validation, and legacy V1
 * migration. No NVS/ESP-IDF dependencies — stations.c wraps this with
 * persistence, matching the station_store.c/stations.c split already used
 * in this codebase.
 */
#pragma once

#include "station_store.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATIONS_V2_MAGIC   0x53544e32u  /* "STN2" */
#define STATIONS_V2_VERSION 2u
#define STATIONS_V1_MAGIC   0x53544131u  /* "STA1" */

/* V2 blob (current format). */
typedef struct {
    uint32_t        magic;
    uint16_t        version;
    uint16_t        header_size;   /* sizeof(header), i.e. offsetof(store) */
    uint32_t        payload_size;  /* sizeof(store) */
    uint32_t        next_id;       /* redundant with store.next_id — FIX3 §8.2 */
    uint32_t        crc32;         /* over `store` only */
    station_store_t store;
} stations_blob_v2_t;

/* V1 blob (legacy format, predates stable IDs) — FIX3 §8.4. The *actual*
 * historical on-disk shape: no IDs, just name/url pairs. Distinct from
 * station_store_t (which has per-item ids and next_id). */
typedef struct {
    char name[STATION_NAME_MAX];
    char url[STATION_URL_MAX];
} station_v1_t;

typedef struct {
    uint32_t     magic;
    int32_t      count;
    station_v1_t items[STATION_MAX];
} stations_blob_v1_t;

typedef enum {
    STATIONS_BLOB_OK = 0,
    STATIONS_BLOB_BAD_SIZE,
    STATIONS_BLOB_BAD_MAGIC,
    STATIONS_BLOB_BAD_VERSION,
    STATIONS_BLOB_BAD_HEADER_SIZE,
    STATIONS_BLOB_BAD_PAYLOAD_SIZE,
    STATIONS_BLOB_BAD_CRC,
    STATIONS_BLOB_BAD_COUNT,
    STATIONS_BLOB_BAD_STRING,
    STATIONS_BLOB_BAD_URL,
    STATIONS_BLOB_DUPLICATE_URL,
    STATIONS_BLOB_BAD_ID,
    STATIONS_BLOB_DUPLICATE_ID,
    STATIONS_BLOB_BAD_NEXT_ID,
} stations_blob_result_t;

/* Standard reflected IEEE CRC-32 (init 0xFFFFFFFF, poly 0xEDB88320, final
 * XOR 0xFFFFFFFF). stations_crc32_ieee("123456789", 9) == 0xCBF43926. The
 * previous implementation shifted right and then tested bit 31, which a
 * right-shift on an unsigned value can never leave set — it always
 * returned 0, silently disabling corruption detection entirely (FIX3
 * STN-001). */
uint32_t stations_crc32_ieee(const void *data, size_t len);

/* Build a fully-formed V2 blob (header + CRC) from a validated store. */
void stations_build_blob(const station_store_t *store, stations_blob_v2_t *blob);

/* Validate every structural invariant before a blob may be published:
 * magic/version/header/payload sizes, CRC, count range, per-item string
 * termination, URL syntax, non-zero unique IDs, unique URLs, and a
 * next_id strictly greater than every active ID (FIX3 §8.2). blob_size
 * must be the exact byte count read from storage — a short/long read is
 * rejected before any field is even examined. */
stations_blob_result_t stations_blob_validate(const stations_blob_v2_t *blob, size_t blob_size);

/* Validate and convert a legacy V1 blob into a fresh V2 store: assigns
 * stable IDs 1..count in original order, next_id = count+1 (FIX3 §8.4).
 * v1_size must be the exact byte count read from storage. Returns true on
 * success with *out populated; false (out untouched) on any invariant
 * violation — count out of range, an unterminated/invalid name or URL, or
 * a duplicate URL. */
bool stations_migrate_v1(const stations_blob_v1_t *v1, size_t v1_size, station_store_t *out);

#ifdef __cplusplus
}
#endif
