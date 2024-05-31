#pragma once

#include "SkipList.hpp"

#include <shared_mutex>
#include <source_location>

class Database {
public:
    [[nodiscard]] static auto query(std::span<const std::byte> data) -> std::vector<std::byte>;

    static auto select(std::string_view statement) -> std::vector<std::byte>;

    [[nodiscard]] auto exists(std::string_view statement) -> std::vector<std::byte>;

    Database(const Database &) = delete;

    Database(Database &&) noexcept;

    auto operator=(const Database &) -> Database = delete;

    auto operator=(Database &&) noexcept -> Database &;

    ~Database();

private:
    static auto initialize() -> std::unordered_map<unsigned long, Database>;

    explicit Database(unsigned long id, std::source_location sourceLocation = std::source_location::current());

    static constexpr std::string filepathPrefix{"data/"};
    static std::unordered_map<unsigned long, Database> databases;

    unsigned long id;
    SkipList skipList;
    std::shared_mutex lock;
};
