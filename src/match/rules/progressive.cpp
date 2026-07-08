#include <common/match/matchrule.hpp>
#include <match/rule_registry.hpp>

namespace match {
    class ProgressiveRule : public MatchRule {
    public:
        void OnCardDrawn(MatchState* state, CardDrawnEvent& event) override {
            event.keep_drawing = true;  // Tell the DrawCard engine to loop
        }
    };

    static RuleRegistrar registrar("progressive", []() {
        return std::make_unique<ProgressiveRule>();
    }, "Progressive", "Keep drawing cards until you draw one that is playable.");
}
