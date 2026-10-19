#include <doctest/doctest.h>

#include <match/expr/checker.hpp>
#include <match/expr/evaluator.hpp>
#include <match/expr/parser.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include "expr_fake_context.hpp"
#include "expr_test_support.hpp"

namespace {

using match::expr::CheckParsedExpression;
using match::expr::CheckResult;
using match::expr::EvalResult;
using match::expr::Evaluate;
using match::expr::GuardState;
using match::expr::ParseExpression;
using match::expr::ParseResult;
using match::expr::Schema;
using match::expr::Type;
using match::expr::Value;
using match::expr::testing::FakeContext;
using match::expr::testing::MakeTestSchema;

// INFO: checks and evaluates one parsed tree so the checker's widening hint on
// the tree reaches the evaluator, exactly as a real caller does.
Value CheckedValue(std::string_view source, const Schema& schema, FakeContext& context,
                   Type expected) {
    const ParseResult parsed = ParseExpression(source);
    REQUIRE_FALSE(parsed.error.has_value());
    REQUIRE(parsed.root != nullptr);
    const CheckResult checked =
        CheckParsedExpression(*parsed.root, schema, expected, "test", "/", GuardState{});
    REQUIRE(checked.diagnostics.empty());
    const EvalResult result = Evaluate(*parsed.root, context);
    REQUIRE(std::holds_alternative<Value>(result));
    return std::get<Value>(result);
}

}  // namespace

TEST_SUITE("expr widen") {

TEST_CASE("if_widens_an_int_branch_to_float") {
    const Schema schema = MakeTestSchema();
    FakeContext context;
    CHECK(std::get<double>(CheckedValue("if(true, 1, 2.5) / 2", schema, context, Type::kFloat)) ==
          0.5);
    CHECK(std::get<double>(CheckedValue("if(false, 1, 2.5) / 2", schema, context, Type::kFloat)) ==
          1.25);
}

TEST_CASE("an_int_expression_checked_as_float_widens") {
    const Schema schema = MakeTestSchema();
    FakeContext context;
    const Value value = CheckedValue("1", schema, context, Type::kFloat);
    REQUIRE(std::holds_alternative<double>(value));
    CHECK(std::get<double>(value) == 1.0);
}

TEST_CASE("a_recheck_resets_the_widening_flag") {
    Schema float_schema = MakeTestSchema();
    float_schema.variables["value"] = match::expr::VariableInfo{Type::kFloat, false};
    Schema int_schema = MakeTestSchema();
    int_schema.variables["value"] = match::expr::VariableInfo{Type::kInt, false};

    const ParseResult parsed = ParseExpression("if(true, $value, 1)");
    REQUIRE_FALSE(parsed.error.has_value());
    const CheckResult checked_float = CheckParsedExpression(*parsed.root, float_schema,
                                                           std::nullopt, "test", "/",
                                                           GuardState{});
    CHECK(checked_float.type == Type::kFloat);
    const CheckResult checked_int =
        CheckParsedExpression(*parsed.root, int_schema, std::nullopt, "test", "/", GuardState{});
    CHECK(checked_int.type == Type::kInt);

    FakeContext context;
    context.variables["value"] = Value{int64_t{1}};
    const EvalResult result = Evaluate(*parsed.root, context);
    REQUIRE(std::holds_alternative<Value>(result));
    CHECK(std::holds_alternative<int64_t>(std::get<Value>(result)));
}

TEST_CASE("sum_of_an_empty_float_set_is_float_zero") {
    const Schema schema = MakeTestSchema();
    FakeContext context;
    const Value value =
        CheckedValue("sum(@all[hand_size > 99], 1.5)", schema, context, Type::kFloat);
    REQUIRE(std::holds_alternative<double>(value));
    CHECK(std::get<double>(value) == 0.0);
}

TEST_CASE("default_widens_an_int_fallback_to_float") {
    Schema schema = MakeTestSchema();
    schema.variables["ratio"] = match::expr::VariableInfo{Type::kFloat, true};
    FakeContext context;
    CHECK(std::get<double>(CheckedValue("default($ratio, 1) / 2", schema, context, Type::kFloat)) ==
          0.5);
    context.variables["ratio"] = Value{1.0};
    CHECK(std::get<double>(CheckedValue("default($ratio, 1) / 2", schema, context, Type::kFloat)) ==
          0.5);
}

}  // TEST_SUITE
