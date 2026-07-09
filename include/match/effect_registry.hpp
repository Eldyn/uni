#pragma once
#include <common/match/effect.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <functional>
#include <memory>
#include "logger.hpp"

/**
 * @file effect_registry.hpp
 * @brief Self-registration system for match effects, mirroring RuleRegistry.
 *
 * Each effect's .cpp defines a static EffectRegistrar that inserts its
 * factory into EffectRegistry at startup. EffectRegistry::Create(json)
 * dispatches on the "type" field without a central switch statement.
 */

namespace match {

    using EffectFactory = std::function<std::unique_ptr<Effect>(const nlohmann::json&)>;

    /**
     * @class EffectRegistry
     * @brief Global registry mapping effect string id → factory (Meyers' Singleton).
     * @tag EFFECT-REG-CLS-001
     */
    class EffectRegistry {
    public:
        static std::unordered_map<std::string, EffectFactory>& GetMap() {
            static std::unordered_map<std::string, EffectFactory> registry;
            return registry;
        }

        static std::unique_ptr<Effect> Create(const nlohmann::json& e) {
            std::string type = e.value("type", std::string{});
            auto it = GetMap().find(type);
            if (it == GetMap().end()) {
                Logger::Warn("[EffectRegistry] Unknown effect type: ", type);
                return nullptr;
            }
            return it->second(e);
        }
    };

    /**
     * @struct EffectRegistrar
     * @brief Static initializer that registers an effect factory at program startup.
     *        Place one static instance at the bottom of each effect's .cpp.
     * @tag EFFECT-REG-STR-001
     */
    struct EffectRegistrar {
        EffectRegistrar(EffectType type, EffectFactory factory) {
            EffectRegistry::GetMap()[std::string(EffectTypeToString(type))] = std::move(factory);
        }
    };

}  // namespace match
