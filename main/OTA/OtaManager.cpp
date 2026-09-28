#include "OtaManager.h"
#include <string>
#include <cstdio>
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "HttpClient.h"
#include "../FileSystem/FileSystem.h"
#include "../Utils/StrUtils.h"
#include "../Utils/SemVer.h"
#include "../Boards/Board.h"

OtaUpdateInfo OtaManager::info;
std::string OtaManager::lastError;

// Canal de updates da placa (perfil em Boards/<placa>/Board.cpp)
static const char* CELEROS_UPDATE_CHANNEL = Board::profile().otaChannel;

// Fonte canonica dos updates do CelerOS (hub proprio). Trocar aqui (ou usar
// /local/ota_url.txt no dispositivo) para apontar outro servidor.
static const char* CELEROS_UPDATE_BASE =
    "https://os.celer.tec.br/updates";

std::string OtaManager::getUpdateJsonUrl() {
    // Override para testes com servidor local (tools/ota_server.py). Artefato
    // de dev por design: continua como ARQUIVO (gravavel pelo file manager
    // com auth), lido no momento do check — NVS exigiria reboot para trocar
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
    return std::string(CELEROS_UPDATE_BASE) + "/" + CELEROS_UPDATE_CHANNEL + "/update.json";
}

// Flash de firmware so por HTTPS, ou HTTP com opt-in local explicito
// (/local/ota_allow_http.txt — criado pelo dono via celerctl/file manager
// para o fluxo dev com tools/ota_server.py). Sem isso, gravar
// /local/ota_url.txt apontando para um servidor do atacante bastava para
// instalar firmware arbitrario na proxima atualizacao.
static bool urlSchemeAllowed(const std::string& url) {
    if (kstr::startsWith(url, "https://")) return true;
    return kstr::startsWith(url, "http://") &&
           FileSystem::exists("/local/ota_allow_http.txt");
}

static const char* HTTP_BLOCKED_MSG =
    "OTA bloqueado: http:// requer /local/ota_allow_http.txt no aparelho";

bool OtaManager::checkForUpdates() {
    info = OtaUpdateInfo();
    std::string url = getUpdateJsonUrl();
    if (!urlSchemeAllowed(url)) {
        info.fetchFailed = true;
        lastError = HTTP_BLOCKED_MSG;
        ESP_LOGE("celer.ota", "%s (url: %s)", HTTP_BLOCKED_MSG, url.c_str());
        return false;
    }

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

    if (celer::versionGreater(info.version, CELEROS_VERSION)) {
        info.available = true;
    }
    return info.available;
}

static void setOtaError(const char* stage, esp_err_t err) {
    char msg[64];
    snprintf(msg, sizeof(msg), "OTA failed: %s (0x%X)", stage, (unsigned)err);
    OtaManager::lastError = msg;
    ESP_LOGE("celer.ota", "%s", msg);
}

// Flash direto pelo esp_https_ota (bloqueante). HTTPS valida o servidor
// contra o bundle de CAs; falha em qualquer etapa aborta e deixa o slot
// atual intacto (a imagem so e ativada apos o checksum no finish).
bool OtaManager::performUpdate(const std::string& firmwareUrl, void (*onProgress)(int percent)) {
    lastError = "";

    if (!urlSchemeAllowed(firmwareUrl)) {
        lastError = HTTP_BLOCKED_MSG;
        ESP_LOGE("celer.ota", "%s (url: %s)", HTTP_BLOCKED_MSG, firmwareUrl.c_str());
        return false;
    }

    esp_http_client_config_t http = {};
    http.url = firmwareUrl.c_str();
    if (kstr::startsWith(firmwareUrl, "https://")) http.crt_bundle_attach = esp_crt_bundle_attach;
    esp_https_ota_config_t cfg = {};
    cfg.http_config = &http;

    esp_https_ota_handle_t handle = nullptr;
    esp_err_t err = esp_https_ota_begin(&cfg, &handle);
    if (err != ESP_OK) {
        setOtaError("begin", err);
        return false;
    }

    int lastPct = -1;
    while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        int total = esp_https_ota_get_image_size(handle);
        if (total > 0 && onProgress != nullptr) {
            int pct = (int)((int64_t)esp_https_ota_get_image_len_read(handle) * 100 / total);
            if (pct != lastPct) {
                lastPct = pct;
                onProgress(pct);
            }
        }
    }
    if (err != ESP_OK) {
        esp_https_ota_abort(handle);
        setOtaError("download", err);
        return false;
    }
    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        setOtaError("finish", err);
        return false;
    }
    return true;
}
