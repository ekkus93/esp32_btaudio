#include "cmd_handlers.h"

cmd_status_t cmd_handle_file(const cmd_context_t *ctx)
{
    if (ctx->param_count < 1)
    {
        cmd_send_response("ERR", "FILE", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }

    const char *root = cmd_files_get_root();
    const char *requested = ctx->params[0];
    if ((requested == NULL || requested[0] == '\0'))
    {
        cmd_send_response("ERR", "FILE", "MISSING_PARAM", NULL);
        return CMD_SUCCESS;
    }

    char fullpath[256];
    if (requested[0] == '/')
    {
        cmd_safe_copy(fullpath, sizeof(fullpath), requested);
    }
    else
    {
        if (root == NULL || root[0] == '\0')
        {
            cmd_send_response("ERR", "FILE", "NO_ROOT", NULL);
            return CMD_SUCCESS;
        }
        size_t root_len = strlen(root);
        const char *sep = (root_len > 0 && root[root_len - 1] == '/') ? "" : "/";
        int written = snprintf(fullpath, sizeof(fullpath), "%s%s%s", root, sep, requested);
        if (written < 0 || (size_t)written >= sizeof(fullpath))
        {
            cmd_send_response("ERR", "FILE", "PATH_TOO_LONG", requested);
            return CMD_SUCCESS;
        }
    }

    struct stat file_stat = {0};
    if (stat(fullpath, &file_stat) != 0)
    {
        cmd_send_response("ERR", "FILE", "NOT_FOUND", requested);
        return CMD_SUCCESS;
    }

    if (!S_ISREG(file_stat.st_mode))
    {
        cmd_send_response("ERR", "FILE", "NOT_FILE", requested);
        return CMD_SUCCESS;
    }

    const char *display_name = requested;
    if (requested[0] == '/')
    {
        const char *slash = strrchr(requested, '/');
        if (slash && *(slash + 1) != '\0')
        {
            display_name = slash + 1;
        }
    }

    char data[160];
    snprintf(data, sizeof(data), "%s,%llu", display_name, (unsigned long long)file_stat.st_size);
    cmd_send_response("OK", "FILE", "FOUND", data);
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_files(const cmd_context_t *ctx)
{
    if (ctx->param_count > 0)
    {
        cmd_send_response("ERR", "FILES", "UNEXPECTED_PARAM", ctx->params[0]);
        return CMD_SUCCESS;
    }
    const char *root = cmd_files_get_root();
    if (root == NULL || root[0] == '\0')
    {
        cmd_send_response("ERR", "FILES", "NO_ROOT", NULL);
        return CMD_SUCCESS;
    }

    cmd_send_response("INFO", "FILES", "ROOT", root);

    esp_err_t mount_err = cmd_mount_spiffs_if_needed();
#ifdef ESP_PLATFORM
    if (mount_err != ESP_OK)
    {
        char errbuf[64];
        snprintf(errbuf, sizeof(errbuf), "mount_err=%d", (int)mount_err);
        cmd_send_response("ERR", "FILES", "MOUNT_FAILED", errbuf);
        return CMD_SUCCESS;
    }
#else
    (void)mount_err;
#endif

    DIR *dir = opendir(root);
    if (dir == NULL)
    {
        char errbuf[96];
        int open_err = errno;
#ifndef ESP_PLATFORM
        const char *reason = strerror(open_err);
        snprintf(errbuf, sizeof(errbuf), "%d:%s", open_err, (reason != NULL) ? reason : "UNKNOWN");
#else
        snprintf(errbuf, sizeof(errbuf), "errno=%d", open_err);
#endif
        cmd_send_response("ERR", "FILES", "OPEN_FAILED", errbuf);
        return CMD_SUCCESS;
    }

    int file_count = 0;
    unsigned long long total_size = 0ULL;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;
        if (name == NULL || name[0] == '\0')
        {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        {
            continue;
        }

        char fullpath[256];
        size_t root_len = strlen(root);
        const char *sep = (root_len > 0 && root[root_len - 1] == '/') ? "" : "/";
        int written = snprintf(fullpath, sizeof(fullpath), "%s%s%s", root, sep, name);
        if (written < 0 || (size_t)written >= sizeof(fullpath))
        {
            char warn_name[CMD_FILES_WARN_NAME_MAX];
            copy_truncated_identifier(name, warn_name, sizeof(warn_name));
            char warn[128];
            int warn_written = snprintf(warn, sizeof(warn), "SKIP_LONG_PATH,%s", warn_name);
            if (warn_written < 0 || warn_written >= (int)sizeof(warn))
            {
                cmd_safe_copy(warn, sizeof(warn), "SKIP_LONG_PATH,???");
            }
            cmd_send_response("INFO", "FILES", "SKIP", warn);
            continue;
        }

        struct stat file_stat = {0};
        if (stat(fullpath, &file_stat) != 0)
        {
            char warn_name[CMD_FILES_WARN_NAME_MAX];
            copy_truncated_identifier(name, warn_name, sizeof(warn_name));
            char warn[160];
            int stat_err = errno;
#ifdef ESP_PLATFORM
            int warn_written = snprintf(warn, sizeof(warn), "STAT_FAILED,%s,errno=%d", warn_name, stat_err);
#else
            const char *reason = strerror(stat_err);
            int warn_written = snprintf(warn, sizeof(warn), "STAT_FAILED,%s,%d:%s", warn_name, stat_err, (reason != NULL) ? reason : "UNKNOWN");
#endif
            if (warn_written < 0 || warn_written >= (int)sizeof(warn))
            {
                cmd_safe_copy(warn, sizeof(warn), "STAT_FAILED,???,errno=?");
            }
            cmd_send_response("INFO", "FILES", "SKIP", warn);
            continue;
        }

        if (!S_ISREG(file_stat.st_mode))
        {
            char warn_name[CMD_FILES_WARN_NAME_MAX];
            copy_truncated_identifier(name, warn_name, sizeof(warn_name));
            char warn[128];
            int warn_written = snprintf(warn, sizeof(warn), "NON_FILE,%s", warn_name);
            if (warn_written < 0 || warn_written >= (int)sizeof(warn))
            {
                cmd_safe_copy(warn, sizeof(warn), "NON_FILE,???");
            }
            cmd_send_response("INFO", "FILES", "SKIP", warn);
            continue;
        }

        char item_name[CMD_FILES_ITEM_NAME_MAX];
        copy_truncated_identifier(name, item_name, sizeof(item_name));
        char line[192];
        int line_written = snprintf(line, sizeof(line), "%s,%llu", item_name, (unsigned long long)file_stat.st_size);
        if (line_written < 0 || line_written >= (int)sizeof(line))
        {
            cmd_safe_copy(line, sizeof(line), "???,0");
        }
        cmd_send_response("INFO", "FILES", "ITEM", line);
        file_count++;
        total_size += (unsigned long long)file_stat.st_size;
    }

    closedir(dir);

    char root_label[CMD_FILES_SUMMARY_ROOT_MAX];
    copy_truncated_identifier(root, root_label, sizeof(root_label));
    char summary[192];
    int summary_written = snprintf(summary, sizeof(summary), "ROOT=%s,COUNT=%d,TOTAL=%llu", root_label, file_count, total_size);
    if (summary_written < 0 || summary_written >= (int)sizeof(summary))
    {
        cmd_safe_copy(summary, sizeof(summary), "ROOT=?,COUNT=?,TOTAL=?");
    }
    cmd_send_response("OK", "FILES", "SUMMARY", summary);
    return CMD_SUCCESS;
}

cmd_status_t cmd_handle_parts(const cmd_context_t *ctx)
{
    (void)ctx;
#ifdef ESP_PLATFORM
    esp_partition_iterator_t partition_iter = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (partition_iter == NULL) {
        cmd_send_response("ERR", "PARTS", "NONE", NULL);
        return CMD_SUCCESS;
    }
    int count = 0;
    for (esp_partition_iterator_t cur = partition_iter; cur != NULL; cur = esp_partition_next(cur)) {
        const esp_partition_t *p = esp_partition_get(cur);
        if (p) {
            char data[128];
            const char *label = (p->label[0] != '\0') ? p->label : "<none>";
            int type = (int)p->type;
            int subtype = (int)p->subtype;
            snprintf(data, sizeof(data), "%s,type=%d,sub=%d,off=0x%08x,size=0x%08x", label, type, subtype, (unsigned)p->address, (unsigned)p->size);
            cmd_send_response("INFO", "PARTS", "ITEM", data);
            ++count;
        }
    }
    char summary[64];
    snprintf(summary, sizeof(summary), "COUNT=%d", count);
    cmd_send_response("OK", "PARTS", "SUMMARY", summary);
#else
    cmd_send_response("ERR", "PARTS", "UNSUPPORTED", NULL);
#endif
    return CMD_SUCCESS;
}
