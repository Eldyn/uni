#pragma once
#include <expected>
#include <error.hpp>

/**
 * @file result.hpp
 * @brief Defines aliases for functional error handling via std::expected.
 * * Leverages the C++23 (or backported) `std::expected` feature to return
 * results in a monadic manner, avoiding the overhead of exceptions (try/catch).
 */

/**
 * @typedef Result
 * @brief Represents an operation that may return a value of type T or an Error.
 * @tparam T The type of the data returned on success.
 * @tag RES-TYP-001
 */
template<typename T>
using Result = std::expected<T, Error>;

/**
 * @typedef VoidResult
 * @brief Represents an operation with no return type that may fail by returning an Error.
 * Typically used for DB queries (UPDATE/DELETE) that do not return a data set.
 * @tag RES-TYP-002
 */
using VoidResult = std::expected<void, Error>;
