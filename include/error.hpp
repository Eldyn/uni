#pragma once
#include <string>

using std::string;

struct Error {
    enum class Code {
        kBadRequest,
        kInvalidInput,
        kNotFound,
        kUnauthorised,
        kConflict,
        kDatabaseFailure,
        kInternalError,
    };

    Code   code;
    string message;

    static Error BadRequest   (const string& msg) { return {Code::kBadRequest,       msg}; }
    static Error InvalidInput (const string& msg) { return {Code::kInvalidInput,     msg}; }
    static Error NotFound     (const string& msg) { return {Code::kNotFound,         msg}; }
    static Error Unauthorised (const string& msg) { return {Code::kUnauthorised,     msg}; }
    static Error Conflict     (const string& msg) { return {Code::kConflict,         msg}; }
    static Error DatabaseFail (const string& msg) { return {Code::kDatabaseFailure,  msg}; }
    static Error Internal     (const string& msg) { return {Code::kInternalError,    msg}; }

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
