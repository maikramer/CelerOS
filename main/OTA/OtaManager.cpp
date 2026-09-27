#include "OtaManager.h"
#include <string>
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "HttpClient.h"
#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"

OtaUpdateInfo OtaManager::info;
std::string OtaManager::lastError;

#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN
static const char* KRYONOS_UPDATE_CHANNEL = "smartdisplay_4848S040";
#else
static const char* KRYONOS_UPDATE_CHANNEL = "esp32";
#endif

// Fonte canonica dos updates deste fork. Trocar aqui (ou usar
// /local/ota_url.txt no dispositivo) para apontar outro servidor.
static const char* KRYONOS_UPDATE_BASE =
    "https://raw.githubusercontent.com/maikramer/KryonOS/refs/heads/main/updates";

static bool isVersionGreater(const std::string& newVer, const std::string& oldVer) {
    int newParts[3] = {0,0,0}, oldParts[3] = {0,0,0};
    auto parseV = [](const std::string& v, int* p) {
        int pt = 0, st = 0;
        while(pt<3 && st<(int)v.length()){
            int d = kstr::indexOf(v, '.', st);
            if(d==-1) { p[pt] = (int)kstr::toInt(v.substr(st)); break; }
            p[pt] = (int)kstr::toInt(v.substr(st, d - st));
            st = d+1; pt++;
        }
    };
    parseV(newVer, newParts);
    parseV(oldVer, oldParts);
    if(newParts[0] > oldParts[0]) return true;
    if(newParts[0] < oldParts[0]) return false;
    if(newParts[1] > oldParts[1]) return true;
    if(newParts[1] < oldParts[1]) return false;
    if(newParts[2] > oldParts[2]) return true;
    return false;
}

std::string OtaManager::getUpdateJsonUrl() {
    // Override para testes com servidor local (tools/ota_server.py)
    if (FileSystem::exists("/local/ota_url.txt")) {
        std::string url_Override = kstr::trim(FileSystem::readTextFile("/local/ota_url.txt"));
        if (url_Override.length() > 0) {
            if (!kstr::endsWith(url_Override, ".json")) {
                if (!kstr::endsWith(url_Override, "/")) url_Override += "/";
                url_Override += "update.json";
            }
            return url_Override;
        }
    }
    return std::string(KRYONOS_UPDATE_BASE) + "/" + KRYONOS_UPDATE_CHANNEL + "/update.json";
}

bool OtaManager::checkForUpdates() {
    info = OtaUpdateInfo();
    std::string url = getUpdateJsonUrl();

    HttpClient http;
    http.setTimeout(20000);
    HttpResponse resp = http.get(url);
    if (!resp.isOk()) {
        info.fetchFailed = true;
        return false;
    }
    std::string payload = resp.body;

    info.version = FileSystem::parseJsonValue(payload, "version");
    info.changelog = FileSystem::parseJsonValue(payload, "changelog");
    info.guide = FileSystem::parseJsonValue(payload, "guide");
    info.firmwareUrl = FileSystem::parseJsonValue(payload, "firmware_url");

    info.changelog = kstr::replaceAll(info.changelog, "\\n", "\n");
    info.guide = kstr::replaceAll(info.guide, "\\n", "\n");

    bool major = FileSystem::parseJsonValue(payload, "major_update") == "true";
    bool minor = FileSystem::parseJsonValue(payload, "minor_update") == "true";
    bool security = FileSystem::parseJsonValue(payload, "security_update") == "true";

    if (major) info.type = "Major System Update Available!";
    else if (minor) info.type = "Minor Update Available!";
    else if (security) info.type = "Security Update Available!";
    else info.type = "Update Available!";

    // firmware_url relativa resolve contra o diretorio do update.json
    // (conveniente para o servidor local de testes)
    if (info.firmwareUrl.length() > 0 && !kstr::startsWith(info.firmwareUrl, "http")) {
        int slash = kstr::lastIndexOf(url, '/');
        if (slash >= 0) info.firmwareUrl = url.substr(0, slash + 1) + info.firmwareUrl;
    }
    info.hasFirmware = info.firmwareUrl.length() > 0;

    if (isVersionGreater(info.version, KRYONOS_VERSION)) {
        info.available = true;
    }
    return info.available;
}

bool OtaManager::performUpdate(const std::string& firmwareUrl, void (*onProgress)(int percent)) {
    lastError = "";

    esp_http_client_config_t httpCfg = {};
    httpCfg.url = firmwareUrl.c_str();
    httpCfg.timeout_ms = 30000;
    httpCfg.keep_alive_enable = true;
    if (kstr::startsWith(firmwareUrl, "https:")) {
        httpCfg.crt_bundle_attach = esp_crt_bundle_attach;
        // TLS do canal do OS usa bundle de CA embutido; o escape insecure do
        // sdkconfig cobre os casos sem bundle.
    }

    esp_https_ota_config_t otaCfg = {};
    otaCfg.http_config = &httpCfg;

    esp_https_ota_handle_t handle = nullptr;
    esp_err_t err = esp_https_ota_begin(&otaCfg, &handle);
    if (err != ESP_OK) {
        lastError = std::string("OTA begin: ") + esp_err_to_name(err);
        return false;
    }

    int lastPct = -1;
    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int total = (int)esp_https_ota_get_image_size(handle);
        int read = (int)esp_https_ota_get_image_len_read(handle);
        if (total > 0 && onProgress) {
            int pct = (read * 100) / total;
            if (pct != lastPct) {
                onProgress(pct);
                lastPct = pct;
            }
        }
    }

    if (err != ESP_OK) {
        lastError = std::string("OTA perform: ") + esp_err_to_name(err);
        esp_https_ota_abort(handle);
        return false;
    }
    if (!esp_https_ota_is_complete_data_received(handle)) {
        lastError = "Download incomplete";
        esp_https_ota_abort(handle);
        return false;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        // ESP_ERR_OTA_VALIDATE_FAILED: imagem corrompida — slot permanece intacto
        lastError = std::string("Image verify failed: ") + esp_err_to_name(err);
        return false;
    }

    return true;
}
