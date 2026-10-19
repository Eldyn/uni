#include <doctest/doctest.h>

#include <match/expr/checker.hpp>
#include <match/expr/evaluator.hpp>
#include <match/expr/parser.hpp>

#include <cstdint>
#include <limits>
#include <map>
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
using match::expr::Schema;
using match::expr::Type;
using match::expr::Value;

constexpr std::string_view kFile = "mods/a/rules/x.json";
constexpr std::string_view kPointer = "/rule/steps/0/set";

// INFO: the shared test schema plus the writable properties the assignment
// tests need: a float (an int widens to it), non-number properties and a card
// member slot.
Schema MakeSchema() {
    Schema schema = match::expr::testing::MakeTestSchema();
    schema.seat_properties["seat:score"] = Type::kFloat;
    schema.seat_properties["seat:multiplier"] = Type::kFloat;
    schema.seat_properties["seat:ready"] = Type::kBool;
    schema.seat_properties["seat:nickname"] = Type::kString;
    schema.card_slots["uni:marker"] = Type::kMemberSet;
    return schema;
}

std::vector<Diagnostic> Check(std::string_view source) {
    return CheckAssignment(source, MakeSchema(), kFile, kPointer);
}

// INFO: an expression whose value holds exactly `repetitions * 2 + 1` nodes.
std::string RepeatedAddition(size_t repetitions) {
    std::string source = "1";
    for (size_t index = 0; index < repetitions; ++index) {
        source += " + 1";
    }
    return source;
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
// case stores its current value and its result here. The per-seat maps give a
// writable integer and float property so a later seat can fail a computation.
class AssignmentContext : public match::expr::testing::FakeContext {
public:
    int64_t growth = 2;
    std::map<EntityRef, int64_t> counters;
    std::map<EntityRef, double> multipliers;
    bool refuse_writes = false;

    std::optional<Value> ReadProperty(EntityRef entity, std::string_view key) const override {
        if (entity.kind == Type::kMatch && key == "tree:growth") {
            return Value(growth);
        }
        if (key == "seat:counter") {
            const auto found = counters.find(entity);
            if (found != counters.end()) {
                return Value(found->second);
            }
        }
        if (key == "seat:multiplier") {
            const auto found = multipliers.find(entity);
            if (found != multipliers.end()) {
                return Value(found->second);
            }
        }
        return FakeContext::ReadProperty(entity, key);
    }

    bool WriteProperty(EntityRef entity, std::string_view key, const Value& value) override {
        if (refuse_writes) {
            return false;
        }
        if (entity.kind == Type::kMatch && key == "tree:growth") {
            growth = std::get<int64_t>(value);
        }
        if (key == "seat:counter" && std::holds_alternative<int64_t>(value)) {
            counters[entity] = std::get<int64_t>(value);
        }
        if (key == "seat:multiplier" && std::holds_alternative<double>(value)) {
            multipliers[entity] = std::get<double>(value);
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

TEST_CASE("an_int_is_accepted_where_a_float_property_is_expected") {
    CHECK(Check("@next.seat:score = 1").empty());
}

TEST_CASE("an_int_assigned_to_a_float_property_widens_to_double") {
    AssignmentContext context;
    context.multipliers[AssignmentContext::kAna] = 0.0;
    const Assignment assignment = Parse("@next.seat:multiplier = 1");
    CHECK_FALSE(ApplyAssignment(assignment, context).has_value());
    REQUIRE(context.multipliers.count(AssignmentContext::kAna) == 1);
    CHECK(context.multipliers.at(AssignmentContext::kAna) == 1.0);
}

TEST_CASE("compound_assignment_rejects_a_non_number_property") {
    CHECK(FirstMessage(Check("@next.seat:ready *= 1")) == "operator *= needs a number property");
    CHECK(FirstMessage(Check("@next.seat:ready -= 1")) == "operator -= needs a number property");
    CHECK(FirstMessage(Check("@next.seat:nickname *= 1")) ==
          "operator *= needs a number property");
    CHECK(FirstMessage(Check("@self.hand.uni:marker *= 1")) ==
          "operator *= needs a number property");
}

TEST_CASE("compound_assignment_accepts_a_number_property") {
    CHECK(Check("@match.tree:growth -= 1").empty());
    CHECK(Check("@match.tree:growth *= 2").empty());
}

TEST_CASE("an_unknown_property_reports_a_suggestion") {
    const std::vector<Diagnostic> diagnostics = Check("@next.axe:stuned = true");
    CHECK(FirstMessage(diagnostics) == "unknown property \"axe:stuned\" on seat");
    CHECK(diagnostics.front().suggestion == "axe:stunned");
}

TEST_CASE("a_value_that_reads_an_unguarded_optional_variable_errors") {
    CHECK(FirstMessage(Check("@next.axe:stunned = $responder.hand_size > 0")) ==
          "variable $responder may be unbound; guard it with defined($responder)");
}

TEST_CASE("a_defined_guard_allows_the_optional_variable") {
    CHECK(Check("@next.axe:stunned = if(defined($responder), $responder.hand_size > 0, false)")
              .empty());
}

TEST_CASE("an_assignment_over_the_node_budget_is_an_error") {
    const std::vector<Diagnostic> diagnostics =
        Check("@match.tree:growth = " + RepeatedAddition(128));
    CHECK(FirstMessage(diagnostics).find("the limit is 256") != std::string::npos);
}

TEST_CASE("a_later_seat_overflow_reports_an_error_and_writes_nothing") {
    AssignmentContext context;
    context.counters[AssignmentContext::kAna] = 1;
    context.counters[AssignmentContext::kBo] = std::numeric_limits<int64_t>::max();
    context.counters[AssignmentContext::kCy] = 1;
    const Assignment assignment = Parse("@others.seat:counter += 1");
    const std::optional<EvalError> error = ApplyAssignment(assignment, context);
    REQUIRE(error.has_value());
    CHECK(error->code == "integer_overflow");
    CHECK(context.write_log.empty());
}

TEST_CASE("a_later_seat_float_overflow_reports_an_error_and_writes_nothing") {
    AssignmentContext context;
    context.multipliers[AssignmentContext::kAna] = 1.0;
    context.multipliers[AssignmentContext::kBo] = std::numeric_limits<double>::max();
    context.multipliers[AssignmentContext::kCy] = 1.0;
    const Assignment assignment = Parse("@others.seat:multiplier *= 2.0");
    const std::optional<EvalError> error = ApplyAssignment(assignment, context);
    REQUIRE(error.has_value());
    CHECK(error->code == "float_overflow");
    CHECK(context.write_log.empty());
}

TEST_CASE("a_refused_write_reports_write_failed") {
    AssignmentContext context;
    context.refuse_writes = true;
    const Assignment assignment = Parse("@next.axe:stunned = true");
    const std::optional<EvalError> error = ApplyAssignment(assignment, context);
    REQUIRE(error.has_value());
    CHECK(error->code == "write_failed");
}

}  // TEST_SUITE
