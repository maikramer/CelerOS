#ifndef COMMANDCODE_H
#define COMMANDCODE_H

#include <string>
#include <ostream>
#include <map>

/**
 * @file CommandCode.h
 * @brief Defines the CommandCode class for representing and managing device command codes.
 */

/**
 * @class CommandCode
 * @brief Represents a command code with a numeric value and a descriptive name.
 */
class CommandCode {
private:
    uint8_t _value;     /**< The numeric value of the command code. */
    std::string _name;  /**< The name of the command. */

    static std::map<uint8_t, CommandCode> commandDatabase; /**< Database of defined command codes. */
    static bool initialized; /**< Flag to track if the command database has been initialized. */

public:
    /**
     * @brief Default constructor. Creates an invalid command code.
     */
    CommandCode();

    /**
     * @brief Constructor to create a new command code.
     * 
     * @param value The numeric value of the command code.
     * @param name The name of the command.
     */
    CommandCode(uint8_t value, const std::string& name);

    /**
     * @brief Returns the numeric value of the command code.
     *
     * @return The command code value.
     */
    [[nodiscard]] uint8_t value() const;

    /**
     * @brief Returns the name of the command code.
     *
     * @return The command code name.
     */
    [[nodiscard]] const std::string& name() const;

    /**
     * @brief Checks if the command code is valid.
     *
     * @return True if the command code is valid, false otherwise. 
     *         A valid command code has a name.
     */
    [[nodiscard]] bool isValid() const;

    /**
     * @brief Equality operator.
     *
     * @param other The other CommandCode object to compare with.
     * @return True if the command codes have the same value, false otherwise.
     */
    bool operator==(const CommandCode& other) const;

    /**
     * @brief Inequality operator.
     *
     * @param other The other CommandCode object to compare with.
     * @return True if the command codes have different values, false otherwise.
     */
    bool operator!=(const CommandCode& other) const;

    /**
     * @brief Output stream operator overload for printing the command code.
     *
     * @param os The output stream.
     * @param commandCode The CommandCode object to print.
     * @return The output stream.
     */
    friend std::ostream& operator<<(std::ostream& os, const CommandCode& commandCode);

    /**
     * @brief Static method to define a new command code and add it to the database.
     *
     * @param value The numeric value of the command code.
     * @param name The name of the command.
     * @return The newly created CommandCode object.
     */
    static CommandCode define(uint8_t value, const std::string& name);

    /**
     * @brief Static method to retrieve a command code by its value.
     *
     * @param value The numeric value of the command code.
     * @return The CommandCode object matching the value, or an invalid CommandCode if not found.
     */
    static CommandCode get(uint8_t value);

    /**
     * @brief Static method to initialize the command code database.
     */
    static void initialize();
};

#endif // COMMANDCODE_H