//
// Created by maikeu on 03/07/2020.
//

#include "JsonModels.h"
#include "CommonErrorCodes.h"

/**
 * @file JsonModels.cpp
 * @brief Implementation of the JSON data model classes defined in JsonModels.h.
 */

std::ostream &JsonModels::operator<<(std::ostream &Str, const JsonModels::BaseJsonData &v) {
    Str << v.toJson();
    return Str;
}

std::istream &JsonModels::operator>>(std::istream &in, JsonModels::BaseJsonData &v) {
    std::string str;
    in >> str;
    v.fromString(str);
    return in;
}

bool JsonModels::BaseJsonData::fromString(const std::string &jsonStr) {
    try {
    nlohmann::json j = nlohmann::json::parse(jsonStr);
    if (j.is_null()) return false;
    return fromJson(j);
    } catch (const nlohmann::json::exception &e) {
        const char* error_msg = e.what();
        ESP_LOGE(__FUNCTION__, "Exception: %s", error_msg);
        return false;
    }
}

nlohmann::json JsonModels::BaseJsonDataError::getPartialJson(bool force) const {
    nlohmann::json j;
    // A placeholder to ensure we never send an empty JSON object.
    j[""] = "";
    if (ErrorMessage != CommonErrorCodes::None || force) {
        j["Error"] = ErrorMessage != CommonErrorCodes::None;
        j["ErrorMessage"] = ErrorMessage.description();
        j["ErrorName"] = ErrorMessage.name();
    }
    return j;
}

std::string JsonModels::BaseJsonDataError::toJson() const {
    auto j = getPartialJson(false);
    return j.dump();
}

nlohmann::json JsonModels::UpdateDataJson::getPartialUpdateJson(bool force) const {
    auto j = getPartialJson(force);
    j["IsUpdate"] = IsUpdate;
    return j;
}

nlohmann::json JsonModels::BaseListJsonDataBasic::getPartialListJson() const {
    auto j = getPartialJson(false);
    if (Begin) {
        j["Begin"] = Begin;
    }
    if (End) {
        j["End"] = End;
    }
    return j;
}

std::string JsonModels::UuidInfoJsonData::toJson() const {
    nlohmann::json j;
    j["NotifyUUID"] = NotifyUUID;
    j["ServiceUUID"] = ServiceUUID;
    j["WriteUUID"] = WriteUUID;
    return j.dump();
}

bool JsonModels::UuidInfoJsonData::fromJson(const nlohmann::json &j) {
    if (j.is_null()) return false;
    try {
        NotifyUUID = j["NotifyUUID"];
        ServiceUUID = j["ServiceUUID"];
        WriteUUID = j["WriteUUID"];
    } catch (const nlohmann::json::exception &e) {
        const char* error_msg = e.what();
        ESP_LOGE(__FUNCTION__, "Exception: %s", error_msg);
        return false;
    }
    return true;
}
#ifdef USER_MANAGEMENT_ENABLED

std::string JsonModels::LoginTryResultJson::toJson() const {
    auto j = getPartialJson(false);
    if (IsAdmin) {
        j["IsAdmin"] = IsAdmin;
    }
    return j.dump();
}

bool JsonModels::LoginTryResultJson::fromJson(const nlohmann::json &j) {
    if (j.is_null()) return false;
    try {
        IsAdmin = j["IsAdmin"];
    } catch (const nlohmann::json::exception &e) {
        const char* error_msg = e.what();
        ESP_LOGE(__FUNCTION__, "Exception: %s", error_msg);
        return false;
    }
    return true;
}

nlohmann::json JsonModels::User::toPureJson() const {
    nlohmann::json j;
    j["Name"] = Name;
    j["Password"] = Password;
    j["Email"] = Email;
    j["IsConfirmed"] = IsConfirmed;
    j["IsAdmin"] = IsAdmin;
    return j;
}

std::string JsonModels::User::toJson() const {
    return toPureJson().dump();
}

bool JsonModels::User::isValid() const {
    return !Name.empty() && !Password.empty() && !Email.empty();
}

std::string JsonModels::User::toString() const {
    std::stringstream stream;
    stream << "Name: " << Name << "| Password: " << Password << "| Email: " << Email << "| IsConfirmed: "
           << IsConfirmed << "| IsAdmin: " << IsAdmin;
    return stream.str();
}

bool JsonModels::User::fromString(const std::string &jsonStr) {
    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        Name = j["Name"];
        Password = j["Password"];
        Email = j["Email"];
        IsConfirmed = j["IsConfirmed"];
        IsAdmin = j["IsAdmin"];
    } catch (const nlohmann::json::exception &e) {
        const char* error_msg = e.what();
        ESP_LOGE(__FUNCTION__, "Exception: %s", error_msg);
        return false;
    }

    return true;
}

bool JsonModels::User::fromJson(const nlohmann::json &j) {
    if (j.is_null()) return false;
    try {
        Name = j["Name"];
        Password = j["Password"];
        Email = j["Email"];
        IsConfirmed = j["IsConfirmed"];
        IsAdmin = j["IsAdmin"];
    } catch (const nlohmann::json::exception &e) {
        const char* error_msg = e.what();
        ESP_LOGE(__FUNCTION__, "Exception: %s", error_msg);
        return false;
    }
    return true;
}

std::string JsonModels::UserListJsonData::toJson() const {
    auto j = getPartialListJson();
    if (Begin) {
        j["UserName"] = UserName;
    }
    j["UserJson"] = UserJson.toPureJson();
    return j.dump();
}

void JsonModels::UserListJsonData::fromPair(std::string userName, User userJson) {
    UserName = userName;
    UserJson = userJson;
}
#endif