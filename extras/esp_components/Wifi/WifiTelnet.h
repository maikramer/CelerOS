#ifndef WIFITELNET_H
#define WIFITELNET_H

#include <string>
#include <vector>
#include <functional>
#include "Telnet.h"
#include "WifiOta.h"
#include "ErrorCode.h"

/**
 * @file WifiTelnet.h
 * @brief This file defines the WifiTelnet class, providing a Telnet server for remote device control.
 */

/**
 * @class WifiTelnet
 * @brief A Telnet server that allows you to manage and control your ESP32 device remotely.
 *
 * Provides commands for OTA updates, system information, and custom command handling.
 */
class WifiTelnet {
public:
    /**
     * @brief Constructor.
     */
    WifiTelnet();

    /**
     * @brief Destructor.
     */
    ~WifiTelnet();

    /**
     * @brief Initializes and starts the Telnet server.
     *
     * @param ssid The SSID (name) of the WiFi access point to create.
     * @param password The password for the WiFi access point (optional, can be nullptr for an open AP).
     * @param ip The IP address to assign to the access point.
     * @param port The port number for Telnet connections (default: 23).
     * @return ErrorCode indicating success or failure.
     */
    ErrorCode start(const std::string& ssid, const std::string& password,
                    const IPAddress& ip, uint16_t port = 23);

    /**
     * @brief Stops the Telnet server.
     */
    void stop();

    /**
     * @brief Registers a custom command handler.
     *
     * @param command The command string (e.g., "my_command").
     * @param handler The function to call when the command is received.
     *               The handler function should take a vector of string arguments and return an ErrorCode.
     * @return ErrorCode indicating success (CommonErrorCodes::None) or failure (e.g., if the command is already registered).
     */
    [[nodiscard]] ErrorCode registerCommand(const std::string& command, std::function<ErrorCode(const std::vector<std::string>&)> handler);

    /**
     * @brief Handles incoming Telnet connections and data.
     *
     * This method should be called periodically in your main loop to process incoming Telnet requests.
     */
    void handle();

private:
    Telnet _telnet; /**< The underlying Telnet server object. */
    mutable WifiOta _ota; /**< The WifiOta object for handling OTA updates. */
    bool _isRunning;  /**< Flag to indicate whether the Telnet server is running. */

    std::map<std::string, std::function<ErrorCode(const std::vector<std::string>&)>> _commandHandlers; /**< Map of registered command handlers. */

    /**
     * @brief Parses a command string and its arguments.
     *
     * @param commandLine The full command line received from the Telnet client.
     * @return A vector containing the command and its arguments.
     */
    [[nodiscard]] static std::vector<std::string> parseCommandLine(const std::string& commandLine) ;

    /**
     * @brief Executes a Telnet command.
     *
     * @param command The command to execute.
     * @param args The arguments for the command.
     * @return ErrorCode indicating the result of the command execution.
     */
    [[nodiscard]] ErrorCode executeCommand(const std::string& command, const std::vector<std::string>& args) const;

    /**
     * @brief Handles the "help" command, listing available commands.
     *
     * @param args Arguments for the help command (currently ignored).
     * @return ErrorCode indicating success or failure.
     */
    [[nodiscard]] ErrorCode handleHelpCommand([[maybe_unused]] const std::vector<std::string>& args) const;

    /**
     * @brief Handles the "ota" command, starting an OTA update.
     *
     * @param args Arguments for the OTA command (the first argument should be the OTA URL).
     * @return ErrorCode indicating success or failure.
     */
    [[nodiscard]] ErrorCode handleOtaCommand(const std::vector<std::string>& args) const;

    /**
     * @brief Handles the "info" command, displaying system information.
     *
     * @param args Arguments for the info command (currently ignored).
     * @return ErrorCode indicating success or failure.
     */
    [[nodiscard]] ErrorCode handleInfoCommand([[maybe_unused]] const std::vector<std::string>& args) const;

    /**
     * @brief Prints a message to the Telnet client.
     *
     * @param message The message to print.
     */
    void printMessage(const std::string& message) const;
};

#endif // WIFITELNET_H