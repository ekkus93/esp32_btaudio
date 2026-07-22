/* stations_persist_core — see stations_persist_core.h. */
#include "stations_persist_core.h"

#include <string.h>
#include <stddef.h>

uint32_t stations_crc32_ieee(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void stations_build_blob(const station_store_t *store, stations_blob_v2_t *blob)
{
    memset(blob, 0, sizeof(*blob));
    blob->magic = STATIONS_V2_MAGIC;
    blob->version = STATIONS_V2_VERSION;
    blob->header_size = (uint16_t)offsetof(stations_blob_v2_t, store);
    blob->payload_size = (uint32_t)sizeof(blob->store);
    blob->next_id = store->next_id;
    blob->store = *store;
    blob->crc32 = stations_crc32_ieee(&blob->store, sizeof(blob->store));
}

static bool field_terminated(const char *s, size_t cap)
{
    return memchr(s, '\0', cap) != NULL;
}

stations_blob_result_t stations_blob_validate(const stations_blob_v2_t *blob, size_t blob_size)
{
    if (!blob || blob_size != sizeof(*blob)) return STATIONS_BLOB_BAD_SIZE;
    if (blob->magic != STATIONS_V2_MAGIC) return STATIONS_BLOB_BAD_MAGIC;
    if (blob->version != STATIONS_V2_VERSION) return STATIONS_BLOB_BAD_VERSION;
    if (blob->header_size != offsetof(stations_blob_v2_t, store)) {
        return STATIONS_BLOB_BAD_HEADER_SIZE;
    }
    if (blob->payload_size != sizeof(blob->store)) return STATIONS_BLOB_BAD_PAYLOAD_SIZE;
    if (stations_crc32_ieee(&blob->store, sizeof(blob->store)) != blob->crc32) {
        return STATIONS_BLOB_BAD_CRC;
    }
    if (blob->store.count < 0 || blob->store.count > STATION_MAX) {
        return STATIONS_BLOB_BAD_COUNT;
    }
    if (blob->next_id != blob->store.next_id) return STATIONS_BLOB_BAD_NEXT_ID;

    uint32_t max_id = 0;
    for (int i = 0; i < blob->store.count; ++i) {
        const station_t *item = &blob->store.items[i];
        if (!field_terminated(item->name, sizeof(item->name)) ||
            !field_terminated(item->url, sizeof(item->url))) {
            return STATIONS_BLOB_BAD_STRING;
        }
        if (item->id == STATION_ID_NONE) return STATIONS_BLOB_BAD_ID;
        if (station_validate_url(item->url) != STATION_OK) return STATIONS_BLOB_BAD_URL;
        if (item->id > max_id) max_id = item->id;

        for (int j = 0; j < i; ++j) {
            if (blob->store.items[j].id == item->id) return STATIONS_BLOB_DUPLICATE_ID;
            if (strcmp(blob->store.items[j].url, item->url) == 0) {
                return STATIONS_BLOB_DUPLICATE_URL;
            }
        }
    }

    if (blob->store.next_id == STATION_ID_NONE || blob->store.next_id <= max_id) {
        return STATIONS_BLOB_BAD_NEXT_ID;
    }
    return STATIONS_BLOB_OK;
}

bool stations_migrate_v1(const stations_blob_v1_t *v1, size_t v1_size, station_store_t *out)
{
    if (!v1 || !out || v1_size != sizeof(*v1)) return false;
    if (v1->magic != STATIONS_V1_MAGIC) return false;
    if (v1->count < 0 || v1->count > STATION_MAX) return false;

    for (int i = 0; i < v1->count; ++i) {
        if (!field_terminated(v1->items[i].name, sizeof(v1->items[i].name)) ||
            !field_terminated(v1->items[i].url, sizeof(v1->items[i].url))) {
            return false;
        }
    }

    station_store_t candidate;
    station_store_init(&candidate);
    for (int i = 0; i < v1->count; ++i) {
        int idx = -1;
        /* station_store_add() validates the URL, rejects duplicates, and
         * assigns sequential stable IDs (1, 2, 3, ...) in call order —
         * exactly the "IDs 1..count in original order" migration rule. */
        station_result_t r = station_store_add(&candidate, v1->items[i].name,
                                               v1->items[i].url, &idx);
        if (r != STATION_OK || idx != i) {
            return false;
        }
    }
    *out = candidate;
    return true;
}
