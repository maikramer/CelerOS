#ifndef JSONMODELS_H
#define JSONMODELS_H

#include <string>
#include <nlohmann/json.hpp>
#include <ErrorCode.h>
#include "projectConfig.h"
#include "esp_log.h"
#include "sstream"
#include "Enums.h"
#include "CommonErrorCodes.h"

/**
 * @file JsonModels.h
 * @brief This file contains the definitions of various JSON data models and utilities for serialization and deserialization.
 */

class ErrorCode;

/**
 * @namespace JsonModels
 * @brief Contains classes and structures representing JSON data models used in the project.
 */
namespace JsonModels {

    /**
     * @class BaseJsonData
     * @brief Base class for all JSON data model classes.
     */
    class BaseJsonData {
    public:
        /**
         * @brief Converts the object to a JSON string representation.
         * @return JSON string representation of the object.
         */
        virtual std::string toJson() const = 0;

        /**
         * @brief Parses a JSON string and populates the object with its data.
         * @param jsonStr The JSON string to parse.
         * @return True if the parsing and population were successful, false otherwise.
         */
        virtual bool fromString(const std::string &jsonStr);

        /**
         * @brief Populates the object with data from a JSON object.
         * @param j The JSON object to extract data from.
         * @return True if the population was successful, false otherwise.
         */
        virtual bool fromJson(const nlohmann::json &j) = 0;
    };

    /**
     * @brief Overloaded output stream operator to write a BaseJsonData object to an output stream.
     * @param Str The output stream object.
     * @param v The BaseJsonData object to write.
     * @return The output stream object.
     */
    std::ostream &operator<<(std::ostream &Str, JsonModels::BaseJsonData const &v);

    /**
     * @brief Overloaded input stream operator to read a BaseJsonData object from an input stream.
     * @param Str The input stream object.
     * @param v The BaseJsonData object to populate with data.
     * @return The input stream object.
     */
    std::istream &operator>>(std::istream &Str, JsonModels::BaseJsonData &v);

    /**
     * @class UuidInfoJsonData
     * @brief Represents a JSON data model containing UUID information.
     */
    class UuidInfoJsonData : public BaseJsonData {
    public:
        std::string NotifyUUID = ""; /**< Notification UUID. */
        std::string ServiceUUID = ""; /**< Service UUID. */
        std::string WriteUUID = ""; /**< Write UUID. */

        /**
         * @brief Converts the object to a JSON string representation.
         * @return JSON string representation of the object.
         */
        [[nodiscard]] std::string toJson() const override;

        /**
         * @brief Populates the object with data from a JSON object.
         * @param j The JSON object to extract data from.
         * @return True if the population was successful, false otherwise.
         */
        [[nodiscard]] bool fromJson(const nlohmann::json &j) override;
    };

    /**
     * @class BaseJsonDataError
     * @brief Base class for JSON data models that include error information.
     */
    class BaseJsonDataError : public BaseJsonData {
    public:
        ErrorCode ErrorMessage = CommonErrorCodes::None; /**< Error code representing the error (if any). */

        /**
         * @brief Converts the object to a JSON string representation.
         * @return JSON string representation of the object.
         */
        [[nodiscard]] std::string toJson() const override;

    protected:
        /**
         * @brief Gets a partial JSON object containing the error information.
         * @param force If true, the error information will be included even if there's no error.
         * @return A JSON object containing the error information.
         */
        [[nodiscard]] nlohmann::json getPartialJson(bool force) const;
    };

    /**
     * @class UpdateDataJson
     * @brief Represents a JSON data model for update data, including error information and an update flag.
     */
    class UpdateDataJson : public BaseJsonDataError {
    protected:
        UpdateDataJson() = default;

        bool IsUpdate = true; /**< Flag indicating if this is an update operation. */

        /**
         * @brief Gets a partial JSON object containing the update and error information.
         * @param force If true, the error information will be included even if there's no error.
         * @return A JSON object containing the update and error information.
         */
        [[nodiscard]] nlohmann::json getPartialUpdateJson(bool force) const;
    };

    /**
     * @class BaseListJsonDataBasic
     * @brief Base class for JSON data models representing lists of data, including basic list information and error handling.
     */
    class BaseListJsonDataBasic : public BaseJsonDataError {
    public:
        bool End = false; /**< Flag indicating the end of the list. */
        bool Begin = false; /**< Flag indicating the beginning of the list. */
        int Index = 0; /**< Current index in the list. */
    protected:
        /**
         * @brief Gets a partial JSON object containing the list information and error data.
         * @return A JSON object containing the list information and error data.
         */
        [[nodiscard]] nlohmann::json getPartialListJson() const;
    };

    /**
     * @class BaseListJsonData
     * @brief Template base class for JSON data models representing lists with specific key and value types.
     * @tparam Tkey Type of the key in the list.
     * @tparam Tvalue Type of the value in the list.
     */
    template<typename Tkey, typename Tvalue>
    class BaseListJsonData : public BaseListJsonDataBasic {
    public:
        /**
         * @brief Populates the object from a key-value pair.
         * @param first The key.
         * @param second The value.
         */
        virtual void fromPair(Tkey first, Tvalue second) = 0;
    };

#ifdef USER_MANAGEMENT_ENABLED

    /**
     * @class User
     * @brief Represents a user with name, password, email, confirmation, and admin status.
     */
    class User : public BaseJsonData {
    public:
        std::string Name = ""; /**< User's name. */
        std::string Password = ""; /**< User's password. */
        std::string Email = ""; /**< User's email address. */
        bool IsConfirmed = false; /**< Flag indicating if the user's email is confirmed. */
        bool IsAdmin = false; /**< Flag indicating if the user has admin privileges. */

        /**
         * @brief Converts the object to a JSON string representation.
         * @return JSON string representation of the object.
         */
        [[nodiscard]] std::string toJson() const override;

        /**
         * @brief Checks if the user data is valid.
         * @return True if the user data is valid, false otherwise.
         */
        [[nodiscard]] bool isValid() const;

        /**
         * @brief Returns a string representation of the user data.
         * @return String representation of the user data.
         */
        [[nodiscard]] std::string toString() const;

        /**
         * @brief Parses a JSON string and populates the object with its data.
         * @param jsonStr The JSON string to parse.
         * @return True if the parsing and population were successful, false otherwise.
         */
        bool fromString(const std::string &jsonStr) override;

        /**
         * @brief Populates the object with data from a JSON object.
         * @param j The JSON object to extract data from.
         * @return True if the population was successful, false otherwise.
         */
        [[nodiscard]] bool fromJson(const nlohmann::json &j) override;

        /**
             * @brief Converts the object to a pure JSON object (without error information).
             * @return Pure JSON object representation of the user data.
             */
        [[nodiscard]] nlohmann::json toPureJson() const;
    };

    /**
     * @class UserListJsonData
     * @brief Represents a JSON data model for a list of User objects.
     */
    class UserListJsonData : public BaseListJsonData<std::string, User> {
    public:
        std::string UserName = ""; /**< User name. */
        User UserJson; /**< User object. */

        /**
         * @brief Converts the object to a JSON string representation.
         * @return JSON string representation of the object.
         */
        [[nodiscard]] std::string toJson() const override;

        /**
         * @brief Populates the object from a key-value pair (user name and User object).
         * @param userName The user name (key).
         * @param userJson The User object (value).
         */
        void fromPair(std::string userName, User userJson) override;
    };

    /**
     * @class LoginTryResultJson
     * @brief Represents a JSON data model for the result of a login attempt.
     */
    class LoginTryResultJson : public JsonModels::BaseJsonDataError {
    public:
        bool IsAdmin = false; /**< Flag indicating if the logged-in user is an admin. */

        /**
         * @brief Converts the object to a JSON string representation.
         * @return JSON string representation of the object.
         */
        [[nodiscard]] std::string toJson() const override;

        /**
         * @brief Populates the object with data from a JSON object.
         * @param j The JSON object to extract data from.
         * @return True if the population was successful, false otherwise.
         */
        [[nodiscard]] bool fromJson(const nlohmann::json &j) override;
    };

    /**
     * @class SignUpResultJson
     * @brief Represents a JSON data model for the result of a signup attempt (inherits from LoginTryResultJson).
     */
    class SignUpResultJson : public JsonModels::LoginTryResultJson {
    };

#endif

}

#endif //JSONMODELS_H