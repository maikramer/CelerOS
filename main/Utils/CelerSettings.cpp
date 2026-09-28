#include "CelerSettings.h"
#include "../FileSystem/FileSystem.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* CS_TAG = "celer.settings";
static const char* NVS_NS = "settings";

std::string CelerSettings::get(const char* key, const char* def) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return def ? def : "";
    char buf[64];
    size_t len = sizeof(buf);
    std::string out = (def != nullptr) ? def : "";
    if (nvs_get_str(h, key, buf, &len) == ESP_OK && len > 1) {
        out.assign(buf, len - 1);  // len inclui o '\0'
    }
    nvs_close(h);
    return out;
}

bool CelerSettings::set(const char* key, const char* value) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok;
    if (value == nullptr || value[0] == '\0') {
        ok = true;
        nvs_erase_key(h, key);  // ausente nao e erro
    } else {
        ok = nvs_set_str(h, key, value) == ESP_OK;
    }
    ok = nvs_commit(h) == ESP_OK && ok;
    nvs_close(h);
    return ok;
}

bool CelerSettings::erase(const char* key) { return set(key, ""); }

bool CelerSettings::eraseAll() {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_iterator_t it = nullptr;
    nvs_entry_find("nvs", NVS_NS, NVS_TYPE_ANY, &it);
    // coleta primeiro: erase durante a iteracao invalida o iterator
    char keys[16][16];
    int n = 0;
    while (it != nullptr && n < 16) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        snprintf(keys[n], sizeof(keys[n]), "%s", info.key);
        n++;
        nvs_entry_next(&it);
    }
    if (it != nullptr) nvs_release_iterator(it);
    for (int i = 0; i < n; i++) nvs_erase_key(h, keys[i]);
    bool ok = nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

void CelerSettings::migrateLegacy() {
    nvs_flash_init();  // idempotente

    // flags: arquivo presente = "1". strings: conteudo trimado.
    struct Legacy {
        const char* key;
        const char* file;
        bool isFlag;
    };
    const Legacy legacy[] = {
        {"web_on",     "/local/web_on.txt",            true},
        {"nowifi",     "/local/nowifi.txt",            true},
        {"install_sd", "/local/config_install_sd.txt", true},
        {"brightness", "/local/brightness.txt",        false},
    };

    for (const auto& l : legacy) {
        if (!get(l.key).empty()) continue;      // NVS ja tem: arquivo e lixo
        if (!FileSystem::exists(l.file)) continue;
        if (l.isFlag) {
            set(l.key, "1");
        } else {
            std::string v = FileSystem::readTextFile(l.file);
            while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
            if (!v.empty()) set(l.key, v.c_str());
        }
        FileSystem::deleteFile(l.file);
        ESP_LOGI(CS_TAG, "%s migrado para NVS (%s)", l.file, l.key);
    }
}
