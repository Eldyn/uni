#include <common/match/matchrule.hpp>
#include <match/rule_registry.hpp>

namespace match {
    class ForcePlayRule : public MatchRule {
    public:
        void OnCardDrawn(MatchState* state, CardDrawnEvent& event) override {
            event.force_play = true;  // Tell the DrawCard engine to bypass DecideDrawnCardEffect
        }
    };

    static RuleRegistrar registrar("force_play", []() {
        return std::make_unique<ForcePlayRule>();
    }, "Force Play", "If the card you draw is playable, you must play it immediately.");
}
