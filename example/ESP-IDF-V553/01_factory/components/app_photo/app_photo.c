#include "app_photo.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "assets/app_assets.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ui/photo_ui.h"

static const char *TAG = "app_photo";
static char *app_name = "Photo";
static bm_app_desc_t app_obj;
static generic_file_list_t s_photo_files = {0};

#define PHOTO_DIR "/sdcard/photo"

static void app_init(void);
static void app_deinit(void);
static void app_run(void);
static void app_close(void);
static void app_resume(void);
static void app_pause(void);

static void free_photo_list(void)
{
    if (s_photo_files.list != NULL) {
        for (int i = 0; i < s_photo_files.count; i++) {
            free(s_photo_files.list[i]);
        }
        free(s_photo_files.list);
    }

    s_photo_files.list = NULL;
    s_photo_files.count = 0;
}

static bool is_supported_photo(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (ext == NULL) {
        return false;
    }

    return strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0;
}

static esp_err_t scan_photo_dir(const char *dir_path, generic_file_list_t *out)
{
    if (dir_path == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out->list = NULL;
    out->count = 0;

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return ESP_FAIL;
    }

    struct dirent *entry = NULL;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG && is_supported_photo(entry->d_name)) {
            count++;
        }
    }

    if (count == 0) {
        closedir(dir);
        return ESP_ERR_NOT_FOUND;
    }

    char **list = (char **)calloc((size_t)count, sizeof(char *));
    if (list == NULL) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }

    rewinddir(dir);
    int idx = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG || !is_supported_photo(entry->d_name)) {
            continue;
        }

        size_t len = strlen(dir_path) + strlen(entry->d_name) + 2;
        char *full_path = (char *)malloc(len);
        if (full_path == NULL) {
            for (int i = 0; i < idx; i++) {
                free(list[i]);
            }
            free(list);
            closedir(dir);
            return ESP_ERR_NO_MEM;
        }

        snprintf(full_path, len, "%s/%s", dir_path, entry->d_name);
        list[idx++] = full_path;
    }

    closedir(dir);
    out->list = list;
    out->count = idx;
    return ESP_OK;
}

bm_app_desc_t *app_photo_init(bool status_bar_flag)
{
    app_obj.name = app_name;
    app_obj.icon = &img_app_gallery;
    app_obj.init = app_init;
    app_obj.deinit = app_deinit;
    app_obj.run = app_run;
    app_obj.close = app_close;
    app_obj.resume = app_resume;
    app_obj.pause = app_pause;
    app_obj.status_bar_flag = status_bar_flag;

    return &app_obj;
}

static void app_init(void)
{
}

static void app_deinit(void)
{
}

static void app_run(void)
{
    const char *message = NULL;
    free_photo_list();

    if (bsp_sdcard_get_handle() == NULL) {
        message = "TF card is not mounted";
        init_photo_ui_screen(lv_screen_active(), NULL, message);
        return;
    }

    esp_err_t err = scan_photo_dir(PHOTO_DIR, &s_photo_files);
    if (err == ESP_FAIL) {
        message = "No /sdcard/photo folder";
        init_photo_ui_screen(lv_screen_active(), NULL, message);
        return;
    }
    if (err == ESP_ERR_NOT_FOUND) {
        message = "No JPG images in photo folder";
        init_photo_ui_screen(lv_screen_active(), NULL, message);
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan photo dir failed: %s", esp_err_to_name(err));
        message = "Failed to read photo folder";
        init_photo_ui_screen(lv_screen_active(), NULL, message);
        return;
    }

    if (init_photo_player() != ESP_OK) {
        message = "Photo decoder init failed";
        init_photo_ui_screen(lv_screen_active(), NULL, message);
        return;
    }

    init_photo_ui_screen(lv_screen_active(), &s_photo_files, NULL);
}

static void app_close(void)
{
    deinit_photo_player();
    free_photo_list();
}

static void app_resume(void)
{
}

static void app_pause(void)
{
}
