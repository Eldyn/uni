#pragma once

#include <atomic>
#include <random>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

// INFO: a unique directory under the system temp folder; it is removed with
// its contents when the test ends.
struct CardSchemaTempDir {
    std::filesystem::path root;

    CardSchemaTempDir() {
        static std::atomic<unsigned long long> next_id{0};
        static const unsigned long long process_tag = std::random_device{}();
        root = std::filesystem::temp_directory_path() /
               ("uni_cardschema_test_" + std::to_string(process_tag) + "_" +
                std::to_string(next_id.fetch_add(1)));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~CardSchemaTempDir() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    CardSchemaTempDir(const CardSchemaTempDir&) = delete;
    CardSchemaTempDir& operator=(const CardSchemaTempDir&) = delete;

    void Write(const std::string& relative, const std::string& text) const {
        const std::filesystem::path path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("cannot write " + path.string());
        }
        out << text;
    }
};

// INFO: the tiny mod the loader tests read. Tasks 3 to 5 add the remaining
// attribute, kind and deck files.
inline void WriteMiniUni(const CardSchemaTempDir& directory) {
    directory.Write("uni/mod.json",
                    R"({"id":"uni","name":"U","version":"1.0.0","api":"1"})");
}
