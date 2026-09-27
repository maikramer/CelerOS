//
// Created by maikeu on 31/12/2019.
//

#include "Storage.h"

auto Storage::StoreConfig(const std::string &key, const std::string &value, bool overwrite) -> ErrorCode {
    const char *TAG = __FUNCTION__;

    ESP_LOGI(TAG, "Opening config file");

    auto res = StoreKeyValueWithoutCheck(key, value, StorageConst::ConfigFilename, overwrite);
    if (res == ErrorCodes::None) {
        ESP_LOGI(TAG, "Configuracao salva");
    } else if (res == ErrorCodes::Exist) {
        ESP_LOGW(TAG, "Configuracao ja existe");
    } else {
        ESP_LOGE(TAG, "Erro salvando configuracao");
    }

    return res;
}

ErrorCode Storage::LoadConfig(const std::string &key, std::string &value) {
    const char *TAG = __FUNCTION__;
    ESP_LOGI(TAG, "Opening config file");
    auto result = ReadKeyFromFile(key, value, StorageConst::ConfigFilename);

    return result;
}
