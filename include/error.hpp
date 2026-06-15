#pragma once
#include <string>

using std::string;

/**
 * @file error.hpp
 * @brief Definition of the standard system and protocol errors.
 */

/**
 * @struct Error
 * @brief Encapsulates the information concerning an error that occurred in the system.
 * * Contains a standard internal code and a descriptive message for easy
 * routing and response to the client.
 * @tag ERR-STR-001
 */
struct Error {

    /**
     * @enum Code
     * @brief Generic codes that classify the type of error.
     * @tag ERR-ENUM-001
     */
    enum class Code {
        kBadRequest,        /**< Malformed request. */
        kInvalidInput,      /**< Invalid input data (e.g. type mismatch in JSON). */
        kNotFound,          /**< Resource not found (e.g. non-existent file or lobby). */
        kUnauthorised,      /**< Lack of permissions or invalid JWT token. */
        kConflict,          /**< Conflict in the resource state (e.g. user already present). */
        kDatabaseFailure,   /**< Internal error in the SQLite driver. */
        kInternalError,     /**< Unhandled exception or server logic error. */
    };

    Code   code;            /**< Type of the error. */
    string message;         /**< Human-readable textual detail. */

    // --- Factory Methods ---

    /** @brief Creates an error of type kBadRequest. @tag ERR-MTH-001 */
    static Error BadRequest   (const string& msg) { return {Code::kBadRequest,       msg}; }
    /** @brief Creates an error of type kInvalidInput. @tag ERR-MTH-002 */
    static Error InvalidInput (const string& msg) { return {Code::kInvalidInput,     msg}; }
    /** @brief Creates an error of type kNotFound. @tag ERR-MTH-003 */
    static Error NotFound     (const string& msg) { return {Code::kNotFound,         msg}; }
    /** @brief Creates an error of type kUnauthorised. @tag ERR-MTH-004 */
    static Error Unauthorised (const string& msg) { return {Code::kUnauthorised,     msg}; }
    /** @brief Creates an error of type kConflict. @tag ERR-MTH-005 */
    static Error Conflict     (const string& msg) { return {Code::kConflict,         msg}; }
    /** @brief Creates a DB error kDatabaseFailure. @tag ERR-MTH-006 */
    static Error DatabaseFail (const string& msg) { return {Code::kDatabaseFailure,  msg}; }
    /** @brief Creates a generic error kInternalError. @tag ERR-MTH-007 */
    static Error Internal     (const string& msg) { return {Code::kInternalError,    msg}; }

    /**
     * @brief Maps the internal error code to the corresponding HTTP Status Code.
     * Useful for automatically responding to the client in a standard way (e.g. in REST APIs).
     * @return std::string Formatted status code (e.g. "404 Not Found").
     * @tag ERR-MTH-008
     */
    std::string HttpStatus() const {
        switch (code) {
            case Code::kBadRequest:      return "400 Bad Request";
            case Code::kInvalidInput:    return "422 Unprocessable Entity";
            case Code::kNotFound:        return "404 Not Found";
            case Code::kUnauthorised:    return "401 Unauthorized";
            case Code::kConflict:        return "409 Conflict";
            case Code::kDatabaseFailure: return "500 Internal Server Error";
            default:                     return "500 Internal Server Error";
        }
    }
};
