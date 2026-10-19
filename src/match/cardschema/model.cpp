#include <match/cardschema/model.hpp>

#include <cstddef>

namespace match::cardschema {

namespace {

constexpr std::size_t kMaxLocalNameLength = 32;

bool IsLocalNameCharacter(char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '_';
}

}  // namespace

std::string NamespaceOf(const std::string& identifier) {
    const std::size_t separator = identifier.find(':');
    if (separator == std::string::npos) {
        return "";
    }
    return identifier.substr(0, separator);
}

std::string LocalNameOf(const std::string& identifier) {
    const std::size_t separator = identifier.find(':');
    if (separator == std::string::npos) {
        return "";
    }
    return identifier.substr(separator + 1);
}

bool IsValidLocalName(const std::string& local) {
    if (local.empty() || local.size() > kMaxLocalNameLength) {
        return false;
    }
    for (const char character : local) {
        if (!IsLocalNameCharacter(character)) {
            return false;
        }
    }
    return true;
}

bool IsValidIdentifier(const std::string& identifier) {
    const std::size_t separator = identifier.find(':');
    if (separator == std::string::npos) {
        return false;
    }
    if (identifier.find(':', separator + 1) != std::string::npos) {
        return false;
    }
    const std::string namespaced = identifier.substr(0, separator);
    const std::string local = identifier.substr(separator + 1);
    return IsValidLocalName(namespaced) && IsValidLocalName(local);
}

}  // namespace match::cardschema
