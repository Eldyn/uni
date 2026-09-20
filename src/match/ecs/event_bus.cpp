#include <match/ecs/event_bus.hpp>

#include <common/env.hpp>
#include <logger.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace match::ecs {
namespace {

/** INFO: guard restoring a counter/stack slot when a dispatch scope exits. */
template <typename T>
struct RestoreGuard {
    T& slot;
    ~RestoreGuard() { --slot; }
};

struct PopGuard {
    std::vector<std::string>& stack;
    ~PopGuard() { stack.pop_back(); }
};

}  // namespace

EventBus::EventBus() : EventBus(std::vector<std::string>()) {}

EventBus::EventBus(std::vector<std::string> mod_order)
    : mod_order_(std::move(mod_order)) {
    reentry_cap_ =
        static_cast<uint32_t>(Env::GetInt("UNI_REENTRY_CAP", 8));
    disarm_threshold_ =
        static_cast<uint32_t>(Env::GetInt("UNI_MOD_DISARM_THRESHOLD", 3));
}

void EventBus::SetModOrder(std::vector<std::string> mod_order) {
    mod_order_ = std::move(mod_order);
}

bool EventBus::Subscribe(const std::string& mod_id,
                         uint32_t registration_index,
                         const HookId& hook,
                         HookCallback callback) {
    if (mod_id.empty() || !callback) return false;
    // INFO: reject an unknown hook so a typo cannot silently never fire.
    if (!IsKnownHook(hook)) return false;
    Subscription sub;
    sub.mod_id = mod_id;
    sub.registration_index = registration_index;
    sub.hook = hook;
    sub.callback = std::move(callback);
    sub.seq = next_seq_++;
    subscriptions_.push_back(std::move(sub));
    return true;
}

std::size_t EventBus::ModPosition(const std::string& mod_id) const {
    for (std::size_t i = 0; i < mod_order_.size(); ++i) {
        if (mod_order_[i] == mod_id) return i;
    }
    return std::numeric_limits<std::size_t>::max();
}

std::string EventBus::FirstSubscriberFor(const HookId& hook) const {
    for (const Subscription& sub : subscriptions_) {
        if (sub.hook == hook) return sub.mod_id;
    }
    return "";
}

std::string EventBus::CurrentMod() const {
    return execution_stack_.empty() ? std::string() : execution_stack_.back();
}

HookDispatchResult EventBus::DispatchBefore(const HookId& hook,
                                            HookPayload& payload) {
    return Dispatch(hook, HookPhase::kBefore, payload);
}

HookDispatchResult EventBus::DispatchAfter(const HookId& hook,
                                           HookPayload& payload) {
    return Dispatch(hook, HookPhase::kAfter, payload);
}

HookDispatchResult EventBus::Dispatch(const HookId& hook, HookPhase phase,
                                      HookPayload& payload) {
    HookDispatchResult result;

    // INFO: the phase is authoritative here (the method, not the caller's
    //       HookId::phase, selects before vs after); subscriptions match on
    //       name + this phase and the payload carries the resolved identity.
    const HookId key{hook.name, phase};
    payload.hook = key;
    // INFO: clear any stale veto on a reused payload before before-hooks run.
    if (phase == HookPhase::kBefore) payload.veto = false;

    // WARN: the re-entry guard runs before any callback; breaching it aborts
    //       the chain through the budget path without invoking a hook.
    uint32_t& depth = reentry_depth_[key];
    if (depth >= reentry_cap_) {
        result.aborted = true;
        std::string mod = CurrentMod();
        if (mod.empty()) mod = FirstSubscriberFor(key);
        if (!mod.empty()) AttributeAbort(mod, result);
        Logger::Error("[EventBus] re-entry cap ", reentry_cap_,
                      " exceeded for ", HookPhaseToken(phase), ":",
                      key.name);
        return result;
    }

    ++depth;
    RestoreGuard<uint32_t> depth_guard{depth};

    // INFO: Ordering is the frozen contract:
    //       frozen mod-list order, then registration index, then subscribe
    //       order. stable_sort keeps equal keys in subscribe order.
    std::vector<const Subscription*> ordered;
    ordered.reserve(subscriptions_.size());
    for (const Subscription& sub : subscriptions_) {
        if (sub.hook == key) ordered.push_back(&sub);
    }
    std::stable_sort(
        ordered.begin(), ordered.end(),
        [this](const Subscription* a, const Subscription* b) {
            const std::size_t pa = ModPosition(a->mod_id);
            const std::size_t pb = ModPosition(b->mod_id);
            if (pa != pb) return pa < pb;
            if (a->registration_index != b->registration_index) {
                return a->registration_index < b->registration_index;
            }
            return a->seq < b->seq;
        });

    for (const Subscription* sub : ordered) {
        if (IsDisarmed(sub->mod_id)) continue;
        execution_stack_.push_back(sub->mod_id);
        {
            PopGuard stack_guard{execution_stack_};
            sub->callback(payload);
        }
        ++result.ran;
    }

    if (phase == HookPhase::kBefore) {
        result.vetoed = IsVetoCapable(key) && payload.veto;
    }
    return result;
}

bool EventBus::IsDisarmed(const std::string& mod_id) const {
    return disarmed_.find(mod_id) != disarmed_.end();
}

uint32_t EventBus::AbortCount(const std::string& mod_id) const {
    auto it = abort_counts_.find(mod_id);
    return it == abort_counts_.end() ? 0 : it->second;
}

uint32_t EventBus::CurrentReentryDepth(const HookId& hook) const {
    auto it = reentry_depth_.find(hook);
    return it == reentry_depth_.end() ? 0 : it->second;
}

bool EventBus::NoteChainAbort(const std::string& mod_id) {
    if (mod_id.empty()) return false;
    const uint32_t count = ++abort_counts_[mod_id];
    if (count >= disarm_threshold_ && !IsDisarmed(mod_id)) {
        disarmed_.insert(mod_id);
        Logger::Error("[EventBus] mod '", mod_id, "' disarmed after ", count,
                      " chain abort(s)");
        return true;
    }
    return false;
}

void EventBus::AttributeAbort(const std::string& mod_id,
                              HookDispatchResult& result) {
    result.aborted_mod = mod_id;
    if (NoteChainAbort(mod_id)) result.disarmed_mod = mod_id;
}

}  // namespace match::ecs
