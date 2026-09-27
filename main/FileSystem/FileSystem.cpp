#include "FileSystem.h"
#include "../Utils/StrUtils.h"

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"

static const char* FS_TAG = "kryon.fs";

// Handle do cartao para unmount; bus SPI inicializada sob demanda
static sdmmc_card_t* s_sd_card = nullptr;
static bool s_spi_bus_ready = false;

#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN
  #define KRYONOS_SD_SPI_HOST SPI2_HOST  // FSPI (compartilhada com init 3-wire do painel)
#else
  #define KRYONOS_SD_SPI_HOST SPI2_HOST  // HSPI no ESP32 classico
#endif

bool FileSystem::init() {
    bool success = true;

    // --- LittleFS em /local ---
    esp_vfs_littlefs_conf_t lfsc = {};
    lfsc.base_path = "/local";
    lfsc.partition_label = "littlefs";
    lfsc.format_if_mount_failed = true;
    lfsc.dont_mount = false;
    esp_err_t err = esp_vfs_littlefs_register(&lfsc);
    if (err != ESP_OK) {
        ESP_LOGE(FS_TAG, "LittleFS mount falhou: %s", esp_err_to_name(err));
        success = false;
    } else {
        size_t total = 0, used = 0;
        esp_littlefs_info("littlefs", &total, &used);
        ESP_LOGI(FS_TAG, "LittleFS montado em /local (%u/%u usado)", (unsigned)used, (unsigned)total);
        mkdir("/local/apps");  // ignora EEXIST
    }

    // --- SD em /sd ---
    if (!mountSD()) {
        ESP_LOGW(FS_TAG, "SD Card Mount Failed");
        success = false;
    }

    return success;
}

bool FileSystem::mountSD() {
    if (s_sd_card != nullptr) return true;

    if (!s_spi_bus_ready) {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = KRYON_SD_MOSI;
        buscfg.miso_io_num = KRYON_SD_MISO;
        buscfg.sclk_io_num = KRYON_SD_SCK;
        buscfg.quadwp_io_num = -1;
        buscfg.quadhd_io_num = -1;
        buscfg.max_transfer_sz = 4092;
        esp_err_t err = spi_bus_initialize(KRYONOS_SD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            ESP_LOGE(FS_TAG, "spi_bus_initialize falhou: %s", esp_err_to_name(err));
            return false;
        }
        s_spi_bus_ready = true;
    }

    // IDF 6: esp_vfs_fat_sdspi_mount continua "all-in-one" — anexa o
    // dispositivo do slot_config no bus indicado por host.slot. O detalhe
    // critico e slot.host_id: com zero-init vai para o SPI1 (flash!) e falha
    // com "invalid host".
    static bool s_isr_installed = false;
    if (!s_isr_installed) {
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        s_isr_installed = true;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = (spi_host_device_t)KRYONOS_SD_SPI_HOST;
    slot.gpio_cs = (gpio_num_t)KRYON_SD_CS;
    slot.gpio_cd = GPIO_NUM_NC;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = KRYONOS_SD_SPI_HOST;
#ifdef KRYONOS_BOARD_SMARTDISPLAY_4IN
    // No SmartDisplay o barramento e compartilhado com o init do painel: 4MHz
    host.max_freq_khz = 4000;
#endif

    esp_vfs_fat_mount_config_t mountcfg = {};
    mountcfg.format_if_mount_failed = false;
    mountcfg.max_files = 5;
    mountcfg.allocation_unit_size = 16 * 1024;

    esp_err_t err = esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &mountcfg, &s_sd_card);
    if (err != ESP_OK) {
        ESP_LOGW(FS_TAG, "sdspi mount falhou: %s", esp_err_to_name(err));
        s_sd_card = nullptr;
        return false;
    }
    ESP_LOGI(FS_TAG, "SD montado em /sd (%s)", s_sd_card->cid.name);
    return true;
}

void FileSystem::unmountSD() {
    if (s_sd_card != nullptr) {
        esp_vfs_fat_sdcard_unmount("/sd", s_sd_card);
        s_sd_card = nullptr;
    }
}

bool FileSystem::formatSD() {
    return false;  // FATFS format em runtime nao suportado (igual ao comportamento original)
}

// ---------------------------------------------------------------------------
// Helpers locais
// ---------------------------------------------------------------------------

static bool pathOk(const char* path) {
    return path != nullptr &&
           (strncmp(path, "/local", 6) == 0 || strncmp(path, "/sd", 3) == 0);
}

std::string FileSystem::readTextFile(const char* path) {
    if (!pathOk(path)) return "";

    FILE* f = fopen(path, "rb");
    if (f == nullptr) return "";

    struct stat st;
    if (fstat(fileno(f), &st) != 0 || st.st_size <= 0 || S_ISDIR(st.st_mode)) {
        fclose(f);
        return "";
    }

    std::string content;
    content.reserve((size_t)st.st_size);

    char buffer[512];
    size_t bytesRead = 0;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        content.append(buffer, bytesRead);
    }

    fclose(f);
    return content;
}

bool FileSystem::writeTextFile(const char* path, const char* content) {
    if (!pathOk(path) || content == nullptr) return false;

    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;

    size_t len = strlen(content);
    bool ok = (fwrite(content, 1, len, f) == len);
    fclose(f);
    return ok;
}

bool FileSystem::appendTextFile(const char* path, const char* content) {
    if (!pathOk(path) || content == nullptr) return false;

    FILE* f = fopen(path, "ab");
    if (f == nullptr) return false;

    size_t len = strlen(content);
    bool ok = (fwrite(content, 1, len, f) == len);
    fclose(f);
    return ok;
}

bool FileSystem::exists(const char* path) {
    if (!pathOk(path)) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

bool FileSystem::deleteFile(const char* path) {
    if (!pathOk(path)) return false;
    return unlink(path) == 0;
}

bool FileSystem::formatLittleFS() {
    ESP_LOGI(FS_TAG, "Formatando LittleFS...");
    return esp_littlefs_format("littlefs") == ESP_OK;
}

int FileSystem::listDir(const char* dirPath, std::string* resultFiles, int maxFiles) {
    if (dirPath == nullptr || resultFiles == nullptr) return 0;

    DIR* dir = opendir(dirPath);
    if (dir == nullptr) return 0;

    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr && count < maxFiles) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        std::string fullPath = dirPath;
        if (!kstr::endsWith(fullPath, "/")) fullPath += "/";
        fullPath += ent->d_name;
        resultFiles[count++] = fullPath;
    }
    closedir(dir);
    return count;
}

int FileSystem::listDirectory(const char* dirPath, FileEntry* entries, int maxEntries) {
    if (dirPath == nullptr || entries == nullptr) return 0;

    DIR* dir = opendir(dirPath);
    if (dir == nullptr) return 0;

    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr && count < maxEntries) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        entries[count].name = ent->d_name;

        std::string fullPath = dirPath;
        if (!kstr::endsWith(fullPath, "/")) fullPath += "/";
        fullPath += entries[count].name;
        entries[count].path = fullPath;

        if (ent->d_type == DT_DIR) {
            entries[count].isDir = true;
        } else if (ent->d_type == DT_UNKNOWN) {
            struct stat st;
            entries[count].isDir = (stat(fullPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
        } else {
            entries[count].isDir = false;
        }

        count++;
    }
    closedir(dir);
    return count;
}

bool FileSystem::readCalData(uint16_t* calData) {
    FILE* f = fopen("/local/touch_cal_p.bin", "rb");
    if (f == nullptr) return false;
    bool ok = (fread(calData, 1, 10, f) == 10);
    fclose(f);
    return ok;
}

bool FileSystem::writeCalData(uint16_t* calData) {
    FILE* f = fopen("/local/touch_cal_p.bin", "wb");
    if (f == nullptr) return false;
    fwrite(calData, 1, 10, f);
    fclose(f);
    return true;
}

bool FileSystem::copyFile(const char* srcPath, const char* dstPath) {
    if (!pathOk(srcPath) || !pathOk(dstPath)) return false;

    FILE* src = fopen(srcPath, "rb");
    if (src == nullptr) return false;

    struct stat st;
    if (fstat(fileno(src), &st) == 0 && S_ISDIR(st.st_mode)) {
        fclose(src);
        return false;
    }

    FILE* dst = fopen(dstPath, "wb");
    if (dst == nullptr) {
        fclose(src);
        return false;
    }

    uint8_t buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (fwrite(buf, 1, n, dst) != n) {
            fclose(src);
            fclose(dst);
            return false;
        }
    }

    fclose(src);
    fclose(dst);
    return true;
}

int FileSystem::countFilesInDir(const char* dirPath) {
    DIR* dir = opendir(dirPath);
    if (dir == nullptr) return 0;

    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        bool isDir;
        if (ent->d_type == DT_DIR) isDir = true;
        else if (ent->d_type == DT_UNKNOWN) {
            std::string full = std::string(dirPath) + "/" + ent->d_name;
            struct stat st;
            isDir = (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
        } else isDir = false;

        if (!isDir) {
            count++;
        } else {
            std::string subPath = dirPath;
            if (!kstr::endsWith(subPath, "/")) subPath += "/";
            subPath += ent->d_name;
            count += countFilesInDir(subPath.c_str());
        }
    }
    closedir(dir);
    return count;
}

bool FileSystem::copyDirectory(const char* srcDir, const char* destDir, void (*progressCb)(int current, int total)) {
    if (!srcDir || !destDir) return false;
    mkdir(destDir);

    DIR* dir = opendir(srcDir);
    if (dir == nullptr) return false;

    static int copiedFiles = 0;
    static int totalFiles = 0;
    static bool isTopLevel = true;

    if (isTopLevel) {
        copiedFiles = 0;
        totalFiles = countFilesInDir(srcDir);
        if (totalFiles == 0) totalFiles = 1;
        isTopLevel = false;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        std::string fileName = ent->d_name;

        std::string srcFilePath = srcDir;
        if (!kstr::endsWith(srcFilePath, "/")) srcFilePath += "/";
        srcFilePath += fileName;

        std::string dstFilePath = destDir;
        if (!kstr::endsWith(dstFilePath, "/")) dstFilePath += "/";
        dstFilePath += fileName;

        bool isDir;
        if (ent->d_type == DT_DIR) isDir = true;
        else if (ent->d_type == DT_UNKNOWN) {
            struct stat st;
            isDir = (stat(srcFilePath.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
        } else isDir = false;

        if (isDir) {
            bool wasTopLevel = isTopLevel;
            isTopLevel = false;
            copyDirectory(srcFilePath.c_str(), dstFilePath.c_str(), progressCb);
            isTopLevel = wasTopLevel;
        } else {
            copyFile(srcFilePath.c_str(), dstFilePath.c_str());
            copiedFiles++;
            if (progressCb) progressCb(copiedFiles, totalFiles);
            taskYIELD();
        }
    }
    closedir(dir);

    isTopLevel = true;
    return true;
}

std::string FileSystem::parseJsonValue(const std::string& json, const char* key) {
    std::string searchKey = std::string("\"") + key + "\"";
    int keyIdx = kstr::indexOf(json, searchKey);
    if (keyIdx == -1) return "";

    int colonIdx = kstr::indexOf(json, ':', keyIdx + (int)searchKey.length());
    if (colonIdx == -1) return "";

    int valStart = colonIdx + 1;
    while (valStart < (int)json.length() && (json[valStart] == ' ' || json[valStart] == '\t')) valStart++;

    if (valStart >= (int)json.length()) return "";

    if (json[valStart] == '"') {
        int valEnd = kstr::indexOf(json, '"', valStart + 1);
        if (valEnd == -1) return "";
        return json.substr(valStart + 1, valEnd - valStart - 1);
    } else {
        int valEnd = valStart;
        while (valEnd < (int)json.length() && json[valEnd] != ',' && json[valEnd] != '}' && json[valEnd] != '\n') valEnd++;
        std::string val = json.substr(valStart, valEnd - valStart);
        return kstr::trim(val);
    }
}

bool FileSystem::mkdir(const char* path) {
    if (!pathOk(path)) return false;
    return ::mkdir(path, 0775) == 0 || errno == EEXIST;
}

bool FileSystem::rmdir(const char* path) {
    if (!pathOk(path)) return false;
    return ::rmdir(path) == 0;
}

bool FileSystem::isDirectory(const char* path) {
    if (!pathOk(path)) return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool FileSystem::isFile(const char* path) {
    if (!pathOk(path)) return false;
    struct stat st;
    return stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

bool FileSystem::renameFile(const char* pathFrom, const char* pathTo) {
    if (!pathOk(pathFrom) || !pathOk(pathTo)) return false;
    return rename(pathFrom, pathTo) == 0;
}

size_t FileSystem::getFileSize(const char* path) {
    if (!pathOk(path)) return 0;
    struct stat st;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) return 0;
    return (size_t)st.st_size;
}

time_t FileSystem::getLastModified(const char* path) {
    if (!pathOk(path)) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

size_t FileSystem::getTotalSpace(const char* drive) {
    if (strncmp(drive, "/sd", 3) == 0) {
        FATFS* fs = nullptr;
        DWORD freeClusters = 0;
        if (f_getfree("0:", &freeClusters, &fs) != FR_OK || fs == nullptr) return 0;
        size_t clusterSize = fs->csize * 512;
        return (size_t)((fs->n_fatent - 2) * clusterSize);
    }
    if (strncmp(drive, "/local", 6) == 0) {
        size_t total = 0, used = 0;
        if (esp_littlefs_info("littlefs", &total, &used) != ESP_OK) return 0;
        return total;
    }
    return 0;
}

size_t FileSystem::getUsedSpace(const char* drive) {
    if (strncmp(drive, "/sd", 3) == 0) {
        size_t total = getTotalSpace(drive);
        size_t freeB = getFreeSpace(drive);
        return total > freeB ? (total - freeB) : 0;
    }
    if (strncmp(drive, "/local", 6) == 0) {
        size_t total = 0, used = 0;
        if (esp_littlefs_info("littlefs", &total, &used) != ESP_OK) return 0;
        return used;
    }
    return 0;
}

size_t FileSystem::getFreeSpace(const char* drive) {
    if (strncmp(drive, "/sd", 3) == 0) {
        FATFS* fs = nullptr;
        DWORD freeClusters = 0;
        if (f_getfree("0:", &freeClusters, &fs) != FR_OK || fs == nullptr) return 0;
        return (size_t)(freeClusters * fs->csize * 512);
    }
    if (strncmp(drive, "/local", 6) == 0) {
        size_t total = 0, used = 0;
        if (esp_littlefs_info("littlefs", &total, &used) != ESP_OK) return 0;
        return total > used ? (total - used) : 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// MD5 — implementacao da ROM da Espressif (mbedtls 3.x esconde o MD5 como
// API privada; a ROM expoe direto e o hash precisa continuar MD5 porque e
// formato publicado: System.getFileMD5 do JS e PIN do Settings)
// ---------------------------------------------------------------------------

#include "esp_rom_md5.h"

std::string FileSystem::getFileMD5(const char* path) {
    if (!pathOk(path)) return "";
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return "";

    md5_context_t ctx;
    esp_rom_md5_init(&ctx);

    uint8_t buffer[512];
    size_t len;
    while ((len = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        esp_rom_md5_update(&ctx, buffer, (uint32_t)len);
    }
    fclose(f);

    uint8_t hash[16];
    esp_rom_md5_final(hash, &ctx);

    std::string hexHash;
    for (int i = 0; i < 16; i++) {
        char buf[3];
        sprintf(buf, "%02x", hash[i]);
        hexHash += buf;
    }
    return hexHash;
}
