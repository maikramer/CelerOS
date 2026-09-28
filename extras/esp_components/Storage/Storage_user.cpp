//
// Created by maikeu on 16/07/2020.
//
#include "projectConfig.h"

#ifdef USER_MANAGEMENT_ENABLED

#include <JsonModels.h>
#include "Storage.h"
#include "map"
#include "UserManager.h"

#define LOG_EXTRA

auto Storage::GetEntriesFromUser(const std::string &user, std::map<int64_t, uint32_t> &map) -> ErrorCode {
    return GetEntriesFromFile(user, map);
}

auto
Storage::StoreUser(const JsonModels::User &user, bool overwrite) -> ErrorCode {
    const char *TAG = __FUNCTION__;

#ifdef LOG_EXTRA
    ESP_LOGI(TAG, "Opening user file");
#endif
    if (CheckFileNameForReserved(user.Name)) {
        return ErrorCodes::ReservedFileName;
    }

    auto res = StoreKeyValueWithoutCheck(user.Name, user, StorageConst::UsersFilename, overwrite);
    if (res == ErrorCodes::None) {
        ESP_LOGI(TAG, "Usuario Salvo");
    } else if (res == ErrorCodes::Exist) {
        ESP_LOGW(TAG, "Usuario ja existe");
    } else {
        ESP_LOGE(TAG, "Erro salvando Usuário");
    }

    return res;
}

ErrorCode Storage::GetAllUsers(std::map<std::string, JsonModels::User> &usersMap) {
    auto result = GetEntriesFromFile(StorageConst::UsersFilename, usersMap);
    if (result == ErrorCodes::FileNotFound || result == ErrorCodes::FileIsEmpty) {
        return ErrorCodes::NoUsersRegistered;
    }

    return result;
}

ErrorCode Storage::LoadUser(const std::string &userName, JsonModels::User &user) {
    return ReadKeyFromFile(userName, user, StorageConst::UsersFilename);
}

#endif