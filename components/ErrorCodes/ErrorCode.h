#ifndef ERRORCODE_H
#define ERRORCODE_H

#include <string>
#include <ostream>
#include <map>
#include <esp_log.h>

/**
 * @file ErrorCode.h
 * @brief Defines the ErrorCode class for representing and managing error codes.
 */

/**
 * @enum ErrorCodeType
 * @brief Categories for grouping error codes.
 */
enum class ErrorCodeType {
    General,          /**< General errors. */
    Network,         /**< Network-related errors. */
    WiFi,            /**< WiFi-specific errors. */
    Socket,           /**< Socket operation errors. */
    File,            /**< File system errors. */
    Memory,          /**< Memory allocation errors. */
    Hardware,        /**< Hardware-related errors. */
    User,            /**< User-related errors (e.g., authentication). */
    Storage,         /**< Storage and file system errors. */
    Project,         /**< Project-specific errors. */
    Communication      /**< Communication errors (e.g., network). */
};

/**
 * @class ErrorCode
 * @brief Represents an error code with name, description, and type.
 */
class ErrorCode {
private:
    std::string _name;         /**< Short name for the error code. */
    std::string _description;  /**< Detailed description of the error. */
    ErrorCodeType _type;       /**< Category of the error. */

    /**
     * @brief Get the error database using "Construct On First Use" idiom.
     * 
     * This avoids the Static Initialization Order Fiasco by ensuring
     * the map is constructed on first access, not at static init time.
     */
    static std::map<std::string, ErrorCode>& getErrorDatabase();
    static bool initialized; /**< Flag to track initialization. */

public:
    /**
     * @brief Default constructor. Creates an invalid error code.
     */
    ErrorCode();

    /**
     * @brief Constructor to create a new error code.
     *
     * @param name The short name of the error code.
     * @param description Detailed description of the error.
     * @param type The category of the error.
     */
    ErrorCode(std::string  name, std::string  description, ErrorCodeType type);

    /**
     * @brief Returns the description of the error code.
     *
     * @return A string containing the error description.
     */
    [[nodiscard]] const std::string& description() const;

    /**
     * @brief Returns the name of the error code.
     *
     * @return A string containing the error name.
     */
    [[nodiscard]] const std::string& name() const;

    /**
     * @brief Returns the type of the error code.
     *
     * @return The ErrorCodeType of the current error.
     */
    [[nodiscard]] ErrorCodeType type() const;

    /**
     * @brief Checks if the current error code is valid.
     *
     * @return True if the error code is valid, false otherwise.
     *         A valid error code has a name.
     */
    [[nodiscard]] bool isValid() const;

    /**
     * @brief Equality operator to compare against another ErrorCode.
     *
     * @param other The ErrorCode object to compare with.
     * @return True if the error codes are equal, false otherwise.
     */
    bool operator==(const ErrorCode& other) const;

    /**
     * @brief Inequality operator to compare against another ErrorCode.
     *
     * @param other The ErrorCode object to compare with.
     * @return True if the error codes are not equal, false otherwise.
     */
    bool operator!=(const ErrorCode& other) const;

    /**
     * @brief Output stream operator overload for printing the error description.
     *
     * @param os The output stream object.
     * @param errorCode The ErrorCode object to print.
     * @return The output stream object.
     */
    friend std::ostream& operator<<(std::ostream& os, const ErrorCode& errorCode);

    /**
     * @brief Static method to define a new error code.
     *
     * This method will add the error code to the global database.
     * If an error code with the same name already exists, it will not be added again.
     *
     * @param name The short name of the error code.
     * @param description Detailed description of the error.
     * @param type The category of the error.
     * @return The newly created ErrorCode, or an invalid ErrorCode if the name already exists.
     */
    static ErrorCode define(const std::string& name, const std::string& description, ErrorCodeType type);

    /**
     * @brief Static method to retrieve an ErrorCode from the database by its name.
     *
     * @param name The name of the error code to retrieve.
     * @return The ErrorCode with the given name, or an invalid ErrorCode if not found.
     */
    static ErrorCode get(const std::string& name);

    /**
     * @brief Static method to initialize the error code database.
     *
     * This should be called once at the start of your application.
     */
    static void initialize();

    /**
    * @brief Logs the error message to the console.
    *
    * @param tag A tag to identify the source of the log message.
    * @param level The log level (ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE).
    * @param additionalMessage An optional additional message to include in the log output.
    */
    void log(const char* tag, esp_log_level_t level = ESP_LOG_ERROR, const std::string& additionalMessage = "") const;
};

#endif // ERRORCODE_H