#pragma once

// Base class for ActionRouter and HttpRouter.
// Enforces non-copyability — both routers own handler state
// and must not be duplicated. Not instantiated directly.
class Router {
public:
    virtual ~Router() = default;

    Router(const Router&)            = delete;
    Router& operator=(const Router&) = delete;

protected:
    Router() = default;
};
