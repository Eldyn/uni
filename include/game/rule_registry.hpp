#pragma once
#include "common/game/gamerule.hpp"
#include <unordered_map>
#include <functional>
#include <string>
#include <memory>
#include "logger.hpp"

namespace game {

    using RuleFactory = std::function<std::unique_ptr<GameRule>()>;

    class RuleRegistry {
    public:
        static std::unordered_map<std::string, RuleFactory>& GetMap() {
            static std::unordered_map<std::string, RuleFactory> registry;
            return registry;
        }

        static std::unique_ptr<GameRule> Create(const std::string& name) {
            auto& map = GetMap();
            if (map.find(name) != map.end()) {
                return map[name]();
            }
            Logger::Error("[RuleRegistry] Failed to find rule: ", name);
            return nullptr;
        }
    };

    struct RuleRegistrar {
        RuleRegistrar(const std::string& name, RuleFactory factory) {
            RuleRegistry::GetMap()[name] = std::move(factory);
        }
    };
    
}
