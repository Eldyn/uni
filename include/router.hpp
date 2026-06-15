#pragma once

/**
 * @file router.hpp
 * @brief Base class for the router implementations (HTTP and WebSocket).
 */

/**
 * @class Router
 * @brief Non-instantiable base interface for all the application's routers.
 * * This class enforces the non-copyability rule. Since both `ActionRouter`
 * and `HttpRouter` own the internal state of the handlers, duplicating them would lead
 * to undefined behaviour and memory corruption.
 * @tag RTR-CLS-001
 */
class Router {
public:
    virtual ~Router() = default;

    /** @brief Deletes the copy constructor. */
    Router(const Router&)            = delete;

    /** @brief Deletes the assignment operator. */
    Router& operator=(const Router&) = delete;

protected:
    /** @brief Protected constructor, instantiable only by derived classes. */
    Router() = default;
};
