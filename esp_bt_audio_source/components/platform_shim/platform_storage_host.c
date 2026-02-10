/**
 * @file platform_storage_host.c
 * @brief Host implementation of platform storage API (in-memory key-value store)
 */

#include "platform_storage.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief Storage value types
 */
typedef enum {
    STORAGE_TYPE_I32,
    STORAGE_TYPE_STR,
    STORAGE_TYPE_BLOB
} storage_value_type_t;

/**
 * @brief Storage entry (key-value pair)
 */
typedef struct storage_entry {
    char* key;
    storage_value_type_t type;
    union {
        int32_t i32_val;
        char* str_val;
        struct {
            void* data;
            size_t len;
        } blob_val;
    } value;
    struct storage_entry* next;
} storage_entry_t;

/**
 * @brief Storage namespace
 */
typedef struct storage_namespace {
    char* name;
    storage_entry_t* entries;
    struct storage_namespace* next;
} storage_namespace_t;

static storage_namespace_t* s_namespaces = NULL;

esp_err_t platform_storage_init(void) {
    // No-op for host (in-memory storage)
    return ESP_OK;
}

esp_err_t platform_storage_erase(void) {
    // Free all namespaces and entries
    storage_namespace_t* ns = s_namespaces;
    while (ns) {
        storage_namespace_t* next_ns = ns->next;
        
        // Free all entries in this namespace
        storage_entry_t* entry = ns->entries;
        while (entry) {
            storage_entry_t* next_entry = entry->next;
            free(entry->key);
            if (entry->type == STORAGE_TYPE_STR) {
                free(entry->value.str_val);
            } else if (entry->type == STORAGE_TYPE_BLOB) {
                free(entry->value.blob_val.data);
            }
            free(entry);
            entry = next_entry;
        }
        
        free(ns->name);
        free(ns);
        ns = next_ns;
    }
    
    s_namespaces = NULL;
    return ESP_OK;
}

static storage_namespace_t* find_namespace(const char* name) {
    storage_namespace_t* ns = s_namespaces;
    while (ns) {
        if (strcmp(ns->name, name) == 0) {
            return ns;
        }
        ns = ns->next;
    }
    return NULL;
}

static storage_namespace_t* create_namespace(const char* name) {
    storage_namespace_t* ns = (storage_namespace_t*)calloc(1, sizeof(storage_namespace_t));
    if (!ns) {
        return NULL;
    }
    
    ns->name = strdup(name);
    if (!ns->name) {
        free(ns);
        return NULL;
    }
    
    ns->entries = NULL;
    ns->next = s_namespaces;
    s_namespaces = ns;
    
    return ns;
}

esp_err_t platform_storage_open(const char* namespace, platform_storage_mode_t mode, platform_storage_handle_t* handle) {
    (void)mode; // Unused in host implementation
    
    storage_namespace_t* ns = find_namespace(namespace);
    if (!ns) {
        ns = create_namespace(namespace);
        if (!ns) {
            return ESP_ERR_NO_MEM;
        }
    }
    
    *handle = (platform_storage_handle_t)((uintptr_t)ns);
    return ESP_OK;
}

esp_err_t platform_storage_close(platform_storage_handle_t handle) {
    (void)handle; // No-op for host (namespace remains in memory)
    return ESP_OK;
}

static storage_entry_t* find_entry(storage_namespace_t* ns, const char* key) {
    storage_entry_t* entry = ns->entries;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

static storage_entry_t* create_or_get_entry(storage_namespace_t* ns, const char* key) {
    storage_entry_t* entry = find_entry(ns, key);
    if (entry) {
        // Free old value if it exists
        if (entry->type == STORAGE_TYPE_STR && entry->value.str_val) {
            free(entry->value.str_val);
            entry->value.str_val = NULL;
        } else if (entry->type == STORAGE_TYPE_BLOB && entry->value.blob_val.data) {
            free(entry->value.blob_val.data);
            entry->value.blob_val.data = NULL;
        }
        return entry;
    }
    
    // Create new entry
    entry = (storage_entry_t*)calloc(1, sizeof(storage_entry_t));
    if (!entry) {
        return NULL;
    }
    
    entry->key = strdup(key);
    if (!entry->key) {
        free(entry);
        return NULL;
    }
    
    entry->next = ns->entries;
    ns->entries = entry;
    
    return entry;
}

esp_err_t platform_storage_get_i32(platform_storage_handle_t handle, const char* key, int32_t* out_value) {
    storage_namespace_t* ns = (storage_namespace_t*)((uintptr_t)handle);
    if (!ns) {
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_entry_t* entry = find_entry(ns, key);
    if (!entry || entry->type != STORAGE_TYPE_I32) {
        return PLATFORM_ERR_STORAGE_NOT_FOUND;
    }
    
    *out_value = entry->value.i32_val;
    return ESP_OK;
}

esp_err_t platform_storage_set_i32(platform_storage_handle_t handle, const char* key, int32_t value) {
    storage_namespace_t* ns = (storage_namespace_t*)((uintptr_t)handle);
    if (!ns) {
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_entry_t* entry = create_or_get_entry(ns, key);
    if (!entry) {
        return ESP_ERR_NO_MEM;
    }
    
    entry->type = STORAGE_TYPE_I32;
    entry->value.i32_val = value;
    return ESP_OK;
}

esp_err_t platform_storage_get_str(platform_storage_handle_t handle, const char* key, char* out_str, size_t* length) {
    storage_namespace_t* ns = (storage_namespace_t*)((uintptr_t)handle);
    if (!ns) {
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_entry_t* entry = find_entry(ns, key);
    if (!entry || entry->type != STORAGE_TYPE_STR) {
        return PLATFORM_ERR_STORAGE_NOT_FOUND;
    }
    
    size_t str_len = strlen(entry->value.str_val) + 1; // Include null terminator
    
    if (out_str == NULL) {
        // Query mode: return required length
        *length = str_len;
        return ESP_OK;
    }
    
    if (*length < str_len) {
        *length = str_len;
        return PLATFORM_ERR_STORAGE_INVALID_LENGTH;
    }
    
    strcpy(out_str, entry->value.str_val);
    *length = str_len;
    return ESP_OK;
}

esp_err_t platform_storage_set_str(platform_storage_handle_t handle, const char* key, const char* value) {
    storage_namespace_t* ns = (storage_namespace_t*)((uintptr_t)handle);
    if (!ns) {
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_entry_t* entry = create_or_get_entry(ns, key);
    if (!entry) {
        return ESP_ERR_NO_MEM;
    }
    
    entry->type = STORAGE_TYPE_STR;
    entry->value.str_val = strdup(value);
    if (!entry->value.str_val) {
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t platform_storage_get_blob(platform_storage_handle_t handle, const char* key, void* out_value, size_t* length) {
    storage_namespace_t* ns = (storage_namespace_t*)((uintptr_t)handle);
    if (!ns) {
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_entry_t* entry = find_entry(ns, key);
    if (!entry || entry->type != STORAGE_TYPE_BLOB) {
        return PLATFORM_ERR_STORAGE_NOT_FOUND;
    }
    
    if (out_value == NULL) {
        // Query mode: return required length
        *length = entry->value.blob_val.len;
        return ESP_OK;
    }
    
    if (*length < entry->value.blob_val.len) {
        *length = entry->value.blob_val.len;
        return PLATFORM_ERR_STORAGE_INVALID_LENGTH;
    }
    
    memcpy(out_value, entry->value.blob_val.data, entry->value.blob_val.len);
    *length = entry->value.blob_val.len;
    return ESP_OK;
}

esp_err_t platform_storage_set_blob(platform_storage_handle_t handle, const char* key, const void* value, size_t length) {
    storage_namespace_t* ns = (storage_namespace_t*)((uintptr_t)handle);
    if (!ns) {
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_entry_t* entry = create_or_get_entry(ns, key);
    if (!entry) {
        return ESP_ERR_NO_MEM;
    }
    
    entry->type = STORAGE_TYPE_BLOB;
    entry->value.blob_val.data = malloc(length);
    if (!entry->value.blob_val.data) {
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(entry->value.blob_val.data, value, length);
    entry->value.blob_val.len = length;
    return ESP_OK;
}

esp_err_t platform_storage_erase_key(platform_storage_handle_t handle, const char* key) {
    storage_namespace_t* ns = (storage_namespace_t*)((uintptr_t)handle);
    if (!ns) {
        return ESP_ERR_INVALID_ARG;
    }
    
    storage_entry_t* prev = NULL;
    storage_entry_t* entry = ns->entries;
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Remove from list
            if (prev) {
                prev->next = entry->next;
            } else {
                ns->entries = entry->next;
            }
            
            // Free entry
            free(entry->key);
            if (entry->type == STORAGE_TYPE_STR) {
                free(entry->value.str_val);
            } else if (entry->type == STORAGE_TYPE_BLOB) {
                free(entry->value.blob_val.data);
            }
            free(entry);
            
            return ESP_OK;
        }
        prev = entry;
        entry = entry->next;
    }
    
    return PLATFORM_ERR_STORAGE_NOT_FOUND;
}

esp_err_t platform_storage_commit(platform_storage_handle_t handle) {
    (void)handle; // No-op for host (in-memory storage is always "committed")
    return ESP_OK;
}
