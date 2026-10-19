#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <match/expr/value.hpp>

namespace match::expr {

// INFO: the runtime side of an expression: which entities the selectors
// resolve to, what properties and variables hold, and the seeded match RNG.
// An empty selector result means "none"; a runtime error skips the step, it
// never returns a silent default.
class EvalContext {
public:
    virtual ~EvalContext() = default;

    virtual std::vector<EntityRef> ResolveSelector(std::string_view name) const = 0;
    virtual std::optional<Value> ReadProperty(EntityRef entity, std::string_view key) const = 0;
    // INFO: a float slot receives a double. An assignment widens an int value to
    // double when the property the write targets already holds a double.
    virtual bool WriteProperty(EntityRef entity, std::string_view key, const Value& value) = 0;
    virtual std::optional<Value> GetVariable(std::string_view name) const = 0;
    virtual bool HasTag(EntityRef entity, std::string_view tag) const = 0;
    // INFO: returns a value in low..high; low..high may span the full int64
    // range, so the context must not compute high - low + 1 unchecked.
    virtual int64_t Random(int64_t low, int64_t high) = 0;
    virtual int64_t StatusMagnitude(EntityRef seat, std::string_view status) const = 0;
};

}  // namespace match::expr
