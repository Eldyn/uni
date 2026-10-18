#include <doctest/doctest.h>

#include <match/expr/checker.hpp>
#include <match/expr/evaluator.hpp>
#include <match/expr/parser.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "expr_fake_context.hpp"
#include "expr_test_support.hpp"

namespace {

using match::expr::ApplyAssignment;
using match::expr::Assignment;
using match::expr::AssignmentResult;
using match::expr::CheckAssignment;
using match::expr::CollectWrittenKeys;
using match::expr::Diagnostic;
using match::expr::EntityRef;
using match::expr::EvalError;
using match::expr::ParseAssignment;
using match::expr::Type;
using match::expr::Value;

constexpr std::string_view kFile = "mods/a/rules/x.json";
constexpr std::string_view kPointer = "/rule/steps/0/set";

std::vector<Diagnostic> Check(std::string_view source) {
    return CheckAssignment(source, match::expr::testing::MakeTestSchema(), kFile, kPointer);
}

std::string FirstMessage(const std::vector<Diagnostic>& diagnostics) {
    REQUIRE_FALSE(diagnostics.empty());
    return diagnostics.front().message;
}

Assignment Parse(std::string_view source) {
    AssignmentResult result = ParseAssignment(source);
    REQUIRE_FALSE(result.error.has_value());
    REQUIRE(result.assignment.has_value());
    return std::move(*result.assignment);
}

// INFO: the shared FakeContext has no match numeric property, so the `+=`
// case stores its current value and its result here.
class AssignmentContext : public match::expr::testing::FakeContext {
public:
    int64_t growth = 2;

    std::optional<Value> ReadProperty(EntityRef entity, std::string_view key) const override {
        if (entity.kind == Type::kMatch && key == "tree:growth") {
            return Value(growth);
        }
        return FakeContext::ReadProperty(entity, key);
    }

    bool WriteProperty(EntityRef entity, std::string_view key, const Value& value) override {
        if (entity.kind == Type::kMatch && key == "tree:growth") {
            growth = std::get<int64_t>(value);
        }
        return FakeContext::WriteProperty(entity, key, value);
    }
};

}  // namespace

TEST_SUITE("expr assignment") {

TEST_CASE("a_namespaced_property_accepts_its_declared_type") {
    CHECK(Check("@next.axe:stunned = true").empty());
}

TEST_CASE("the_value_must_fit_the_property_type") {
    CHECK(FirstMessage(Check("@next.axe:stunned = 1")) == "expected bool, got int");
}

TEST_CASE("a_built_in_property_is_read_only") {
    CHECK(FirstMessage(Check("@next.hand_size = 2")) ==
          "built-in property hand_size is read-only");
}

TEST_CASE("a_compound_assignment_rejects_a_float_for_an_int") {
    CHECK(FirstMessage(Check("@match.tree:growth += 1.5")) ==
          "expected int, got float (use floor(), ceil() or round())");
}

TEST_CASE("compound_assignment_reads_then_writes_the_current_value") {
    AssignmentContext context;
    const Assignment assignment = Parse("@match.tree:growth += 1");
    CHECK_FALSE(ApplyAssignment(assignment, context).has_value());
    CHECK(context.growth == 3);
    REQUIRE(context.write_log.size() == 1);
    CHECK(context.write_log.front() == "tree:growth = 3 on match 0");
}

TEST_CASE("a_filtered_selector_writes_every_selected_seat") {
    AssignmentContext context;
    const Assignment assignment = Parse("@others[hand_size > 3].axe:stunned = true");
    CHECK_FALSE(ApplyAssignment(assignment, context).has_value());
    REQUIRE(context.write_log.size() == 2);
    CHECK(context.write_log[0] == "axe:stunned = true on seat 0");
    CHECK(context.write_log[1] == "axe:stunned = true on seat 2");
}

TEST_CASE("an_empty_target_reports_empty_selector_and_writes_nothing") {
    AssignmentContext context;
    const Assignment assignment = Parse("@others[hand_size > 100].axe:stunned = true");
    const std::optional<EvalError> error = ApplyAssignment(assignment, context);
    REQUIRE(error.has_value());
    CHECK(error->code == "empty_selector");
    CHECK(context.write_log.empty());
}

TEST_CASE("collect_written_keys_is_the_target_property_once") {
    const Assignment assignment = Parse("@others[hand_size > 3].axe:stunned = true");
    CHECK(CollectWrittenKeys(assignment) == std::vector<std::string>{"axe:stunned"});
}

}  // TEST_SUITE
