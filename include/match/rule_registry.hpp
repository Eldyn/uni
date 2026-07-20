#pragma once
#include "common/match/matchrule.hpp"
#include <unordered_map>
#include <functional>
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "logger.hpp"

/**
 * @file rule_registry.hpp
 * @brief Management of the dynamic registration and instantiation of match rules.
 * * This file defines a system based on the Factory Pattern to allow the
 * creation of rules (MatchRule) at runtime starting from their textual name (e.g. from JSON).
 * It uses a static self-registration mechanism to decouple the addition
 * of new rules from the core engine code.
 */

namespace match {

    /**
     * @typedef RuleFactory
     * @brief Defines the type of function capable of generating a new instance of a rule.
     */
    using RuleFactory = std::function<std::unique_ptr<MatchRule>()>;

    /**
     * @struct RuleMetadata
     * @brief Human-readable metadata attached to each registered rule.
     */
    struct RuleMetadata {
        std::string label;
        std::string description;
    };

    /**
     * @struct RuleEntry
     * @brief Pairs a rule factory with its display metadata.
     */
    struct RuleEntry {
        RuleFactory  factory;
        RuleMetadata metadata;
    };

    /**
     * @class RuleRegistry
     * @brief Global (Singleton) registry of the match rule factories.
     * * Maintains a map that associates an identifying string (the name of the mod/rule)
     * with the `RuleEntry` holding the factory and display metadata.
     */
    class RuleRegistry {
    public:
        /**
         * @brief Provides access to the global map of registered rules.
         * Uses the "Meyers' Singleton" pattern to ensure the map is
         * initialized in a thread-safe manner on first use.
         * @return std::unordered_map<std::string, RuleEntry>& Reference to the map of rules.
         */
        static std::unordered_map<std::string, RuleEntry>& GetMap() {
            static std::unordered_map<std::string, RuleEntry> registry;
            return registry;
        }

        /**
         * @brief Instantiates a new match rule from its name.
         * @param name The textual identifier of the rule (e.g. "seven_zero").
         * @return std::unique_ptr<MatchRule> Unique pointer to the new instance, or `nullptr` if not found.
         */
        static std::unique_ptr<MatchRule> Create(const std::string& name) {
            auto& map = GetMap();
            auto it = map.find(name);
            if (it != map.end()) {
                return it->second.factory();
            }
            Logger::Error("[RuleRegistry] Failed to find rule: ", name);
            return nullptr;
        }

        /**
         * @brief Returns a JSON array describing every registered rule (id, label, description).
         * Intended to be sent to clients so they can render dynamic rule selectors.
         * @return nlohmann::json Array of rule descriptor objects.
         */
        static nlohmann::json GetAvailableRulesJson() {
            auto arr = nlohmann::json::array();
            for (auto& [name, entry] : GetMap()) {
                arr.push_back({
                    {"id",          name},
                    {"label",       entry.metadata.label},
                    {"description", entry.metadata.description}
                });
            }
            return arr;
        }
    };

    /**
     * @struct RuleRegistrar
     * @brief Support structure for the self-registration of rules.
     * * This structure is designed to be instantiated globally/statically in the
     * source files (.cpp) of the rules themselves. At program initialization time,
     * the constructor runs and injects the rule into the `RuleRegistry`.
     */
    struct RuleRegistrar {
        /**
         * @brief Constructor that performs the actual registration of the factory and metadata.
         * @param name        The name by which the rule will be known in the system.
         * @param factory     The function able to instantiate the rule.
         * @param label       Short human-readable name shown in the UI.
         * @param description One-sentence description of the rule's effect.
         */
        RuleRegistrar(const std::string& name, RuleFactory factory,
                      std::string label, std::string description) {
            RuleRegistry::GetMap()[name] = {
                std::move(factory),
                { std::move(label), std::move(description) }
            };
        }
    };

}  // namespace match
