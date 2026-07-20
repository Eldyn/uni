#pragma once
#include <string>

/**
 * @file error.hpp
 * @brief Definition of the standard system and protocol errors.
 */

/**
 * @struct Error
 * @brief Encapsulates the information concerning an error that occurred in the system.
 * * Contains a standard internal code and a descriptive message for easy
 * routing and response to the client.
 */
struct Error {
    /**
     * @enum Code
     * @brief Generic codes that classify the type of error.
     */
    enum class Code {
        kBadRequest,        /**< Malformed request. */
        kInvalidInput,      /**< Invalid input data (e.g. type mismatch in JSON). */
        kNotFound,          /**< Resource not found (e.g. non-existent file or lobby). */
        kUnauthorised,      /**< Lack of permissions or invalid JWT token. */
        kConflict,          /**< Conflict in the resource state (e.g. user already present). */
        kDatabaseFailure,   /**< Internal error in the SQLite driver. */
        kInternalError,     /**< Unhandled exception or server logic error. */
        kTooManyRequests,   /**< Rate/attempt limit exceeded (e.g. login throttle lockout). */
    };

    Code        code;            /**< Type of the error. */
    std::string message;         /**< Human-readable textual detail. */

    // --- Factory Methods ---

    /** @brief Creates an error of type kBadRequest. */
    static Error BadRequest   (const std::string& msg) { return {Code::kBadRequest,       msg}; }
    /** @brief Creates an error of type kInvalidInput. */
    static Error InvalidInput (const std::string& msg) { return {Code::kInvalidInput,     msg}; }
    /** @brief Creates an error of type kNotFound. */
    static Error NotFound     (const std::string& msg) { return {Code::kNotFound,         msg}; }
    /** @brief Creates an error of type kUnauthorised. */
    static Error Unauthorised (const std::string& msg) { return {Code::kUnauthorised,     msg}; }
    /** @brief Creates an error of type kConflict. */
    static Error Conflict     (const std::string& msg) { return {Code::kConflict,         msg}; }
    /** @brief Creates a DB error kDatabaseFailure. */
    static Error DatabaseFail (const std::string& msg) { return {Code::kDatabaseFailure,  msg}; }
    /** @brief Creates a generic error kInternalError. */
    static Error Internal     (const std::string& msg) { return {Code::kInternalError,    msg}; }
    /** @brief Creates an error of type kTooManyRequests. */
    static Error TooManyRequests(const std::string& msg) { return {Code::kTooManyRequests, msg}; }

    /**
     * @brief Maps the internal error code to the corresponding HTTP Status Code.
     * Useful for automatically responding to the client in a standard way (e.g. in REST APIs).
     * @return std::string Formatted status code (e.g. "404 Not Found").
     */
    std::string HttpStatus() const {
        switch (code) {
            case Code::kBadRequest:      return "400 Bad Request";
            case Code::kInvalidInput:    return "422 Unprocessable Entity";
            case Code::kNotFound:        return "404 Not Found";
            case Code::kUnauthorised:    return "401 Unauthorized";
            case Code::kConflict:        return "409 Conflict";
            case Code::kDatabaseFailure: return "500 Internal Server Error";
            case Code::kTooManyRequests: return "429 Too Many Requests";
            default:                     return "500 Internal Server Error";
        }
    }
};
