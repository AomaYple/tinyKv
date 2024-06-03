#include "Database.hpp"

#include "../../../common/command/Command.hpp"
#include "../../../common/log/Exception.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <ranges>

static constexpr std::string_view ok{"OK"};
static constexpr std::string_view integer{"(integer) "};
static constexpr std::string_view nil{"(nil)"};

auto Database::query(std::span<const std::byte> data) -> std::vector<std::byte> {
    const auto command{static_cast<Command>(data.front())};
    data = data.subspan(sizeof(command));

    const auto id{*reinterpret_cast<const unsigned long *>(data.data())};
    data = data.subspan(sizeof(id));

    const std::string_view statement{reinterpret_cast<const char *>(data.data()), data.size()};
    switch (command) {
        case Command::select:
            return select(id);
        case Command::del:
            return databases.at(id).del(statement);
        case Command::dump:
            return databases.at(id).dump(statement);
        case Command::exists:
            return databases.at(id).exists(statement);
        case Command::move:
            return databases.at(id).move(statement);
        case Command::rename:
            return databases.at(id).rename(statement);
        case Command::renamenx:
            return databases.at(id).renamenx(statement);
        case Command::type:
            return databases.at(id).type(statement);
        case Command::set:
            return databases.at(id).set(statement);
        case Command::get:
            return databases.at(id).get(statement);
        case Command::getrange:
            return databases.at(id).getRange(statement);
    }

    return {data.cbegin(), data.cend()};
}

auto Database::select(unsigned long id) -> std::vector<std::byte> {
    if (!databases.contains(id)) databases.emplace(id, Database{id});

    const auto spanOk{std::as_bytes(std::span{ok})};
    return {spanOk.cbegin(), spanOk.cend()};
}

Database::Database(Database &&other) noexcept {
    const std::lock_guard lockGuard{other.lock};

    this->id = other.id;
    this->skiplist = std::move(other.skiplist);
}

auto Database::operator=(Database &&other) noexcept -> Database & {
    const std::scoped_lock scopedLock{this->lock, other.lock};

    if (this == &other) return *this;

    this->id = other.id;
    this->skiplist = std::move(other.skiplist);

    return *this;
}

Database::~Database() {
    const std::vector serialization{this->skiplist.serialize()};

    std::ofstream file{filepathPrefix + std::to_string(this->id) + ".db", std::ios::binary};
    file.write(reinterpret_cast<const char *>(serialization.data()), static_cast<long>(serialization.size()));
}

auto Database::del(std::string_view keys) -> std::vector<std::byte> {
    unsigned long count{};

    {
        const std::lock_guard lockGuard{this->lock};

        for (const auto &view : keys | std::views::split(' '))
            if (this->skiplist.erase(std::string_view{view})) ++count;
    }

    std::vector<std::byte> buffer;

    const auto spanInteger{std::as_bytes(std::span{integer})};
    buffer.insert(buffer.cend(), spanInteger.cbegin(), spanInteger.cend());

    const std::string stringCount{std::to_string(count)};
    const auto spanCount{std::as_bytes(std::span{stringCount})};
    buffer.insert(buffer.cend(), spanCount.cbegin(), spanCount.cend());

    return buffer;
}

auto Database::dump(std::string_view key) -> std::vector<std::byte> {
    std::vector<std::byte> serialization;
    {
        const std::shared_lock sharedLock{this->lock};

        const std::shared_ptr entry{this->skiplist.find(key)};
        if (entry != nullptr) serialization = entry->serialize();
    }

    if (!serialization.empty()) {
        serialization.insert(serialization.cbegin(), std::byte{'"'});
        serialization.emplace_back(std::byte{'"'});

        return serialization;
    }

    const auto spanNil{std::as_bytes(std::span{nil})};

    return {spanNil.cbegin(), spanNil.cend()};
}

auto Database::exists(std::string_view keys) -> std::vector<std::byte> {
    unsigned long count{};

    {
        const std::shared_lock sharedLock{this->lock};

        for (const auto &view : keys | std::views::split(' '))
            if (this->skiplist.find(std::string_view{view}) != nullptr) ++count;
    }

    std::vector<std::byte> buffer;

    const auto spanInteger{std::as_bytes(std::span{integer})};
    buffer.insert(buffer.cend(), spanInteger.cbegin(), spanInteger.cend());

    const std::string stringCount{std::to_string(count)};
    const auto spanCount{std::as_bytes(std::span{stringCount})};
    buffer.insert(buffer.cend(), spanCount.cbegin(), spanCount.cend());

    return buffer;
}

auto Database::move(std::string_view statment) -> std::vector<std::byte> {
    const unsigned long result{statment.find(' ')};
    const std::string_view key{statment.substr(0, result)};

    bool success{};
    {
        const auto targetResult{databases.find(std::stoul(std::string{statment.substr(result + 1)}))};
        if (targetResult != databases.cend()) {
            Database &target{targetResult->second};

            const std::scoped_lock scopedLock{this->lock, target.lock};

            std::shared_ptr entry{this->skiplist.find(key)};
            const std::shared_ptr targetEntry{target.skiplist.find(key)};
            if (entry != nullptr && targetEntry == nullptr) {
                this->skiplist.erase(key);
                target.skiplist.insert(std::move(entry));

                success = true;
            }
        }
    }

    const auto spanInteger{std::as_bytes(std::span{integer})};
    std::vector<std::byte> buffer{spanInteger.cbegin(), spanInteger.cend()};
    buffer.emplace_back(success ? std::byte{'1'} : std::byte{'0'});

    return buffer;
}

auto Database::rename(std::string_view statement) -> std::vector<std::byte> {
    const unsigned long result{statement.find(' ')};
    const std::string_view key{statement.substr(0, result)}, newKey{statement.substr(result + 1)};

    std::string response;
    {
        const std::lock_guard lockGuard{this->lock};

        std::shared_ptr entry{this->skiplist.find(key)};
        if (entry != nullptr) {
            this->skiplist.erase(key);

            entry->getKey() = newKey;
            this->skiplist.insert(std::move(entry));

            response = '"' + std::string{ok} + '"';
        } else response = "(error) no such key";
    }
    const auto spanResponse{std::as_bytes(std::span{response})};

    return {spanResponse.cbegin(), spanResponse.cend()};
}

auto Database::renamenx(std::string_view statement) -> std::vector<std::byte> {
    const unsigned long result{statement.find(' ')};
    const std::string_view key{statement.substr(0, result)}, newKey{statement.substr(result + 1)};

    bool success{};
    {
        const std::lock_guard lockGuard{this->lock};

        std::shared_ptr entry{this->skiplist.find(key)};
        const std::shared_ptr otherEntry{this->skiplist.find(newKey)};
        if (entry != nullptr && otherEntry == nullptr) {
            this->skiplist.erase(key);

            entry->getKey() = newKey;
            this->skiplist.insert(std::move(entry));

            success = true;
        }
    }

    const auto spanInteger{std::as_bytes(std::span{integer})};
    std::vector<std::byte> buffer{spanInteger.cbegin(), spanInteger.cend()};
    buffer.emplace_back(success ? std::byte{'1'} : std::byte{'0'});

    return buffer;
}

auto Database::type(std::string_view key) -> std::vector<std::byte> {
    std::string_view response;
    {
        const std::shared_lock sharedLock{this->lock};

        const std::shared_ptr entry{this->skiplist.find(key)};
        if (entry != nullptr) {
            switch (entry->getType()) {
                case Entry::Type::string:
                    response = "string";
                    break;
                case Entry::Type::hash:
                    response = "hash";
                    break;
                case Entry::Type::list:
                    response = "list";
                    break;
                case Entry::Type::set:
                    response = "set";
                    break;
                case Entry::Type::sortedSet:
                    response = "zset";
                    break;
            }
        } else response = "none";
    }
    const auto spanResponse{std::as_bytes(std::span{response})};

    std::vector buffer{std::byte{'"'}};
    buffer.insert(buffer.cend(), spanResponse.cbegin(), spanResponse.cend());
    buffer.emplace_back(std::byte{'"'});

    return buffer;
}

auto Database::set(std::string_view statement) -> std::vector<std::byte> {
    const unsigned long result{statement.find(' ')};
    const std::string_view key{statement.substr(0, result)};
    std::string_view value{statement.substr(result + 2)};
    value.remove_suffix(1);

    {
        const std::lock_guard lockGuard{this->lock};

        this->skiplist.insert(std::make_shared<Entry>(std::string{key}, std::string{value}));
    }

    std::vector buffer{std::byte{'"'}};
    const auto spanOk{std::as_bytes(std::span{ok})};
    buffer.insert(buffer.cend(), spanOk.cbegin(), spanOk.cend());
    buffer.emplace_back(std::byte{'"'});

    return buffer;
}

auto Database::get(std::string_view key) -> std::vector<std::byte> {
    std::string response;
    {
        const std::shared_lock sharedLock{this->lock};

        const std::shared_ptr entry{this->skiplist.find(key)};
        if (entry != nullptr) {
            const Entry::Type type{entry->getType()};
            if (type == Entry::Type::string) response = '"' + entry->getString() + '"';
            else response = "(error) WRONGTYPE Operation against a key holding the wrong kind of value";
        } else response = nil;
    }

    const auto spanResponse{std::as_bytes(std::span{response})};
    return {spanResponse.cbegin(), spanResponse.cend()};
}

auto Database::getRange(std::string_view statement) -> std::vector<std::byte> {
    unsigned long result{statement.find(' ')};
    const std::string_view key{statement.substr(0, result)};
    statement.remove_prefix(result + 1);

    result = statement.find(' ');
    const auto start{std::stol(std::string{statement.substr(0, result)})};
    auto end{std::stol(std::string{statement.substr(result + 1)})};

    std::string response{'"'};
    {
        const std::shared_lock sharedLock{this->lock};

        const std::shared_ptr entry{this->skiplist.find(key)};
        if (entry != nullptr) {
            end = end < 0 ? static_cast<decltype(end)>(entry->getString().size()) + end : end;
            ++end;

            if (start < end) response += entry->getString().substr(start, end - start);
        }
    }
    response += '"';

    const auto spanResponse{std::as_bytes(std::span{response})};

    return {spanResponse.cbegin(), spanResponse.cend()};
}

auto Database::initialize() -> std::unordered_map<unsigned long, Database> {
    std::filesystem::create_directories(filepathPrefix);

    std::unordered_map<unsigned long, Database> databases;
    for (const auto &entry : std::filesystem::directory_iterator{filepathPrefix}) {
        const auto filename{entry.path().filename().string()};
        const auto id{std::stoul(filename.substr(0, filename.size() - 3))};
        databases.emplace(id, Database{id});
    }

    for (unsigned char i{}; i < 16; ++i)
        if (!databases.contains(i)) databases.emplace(i, Database{i});

    return databases;
}

Database::Database(unsigned long id, std::source_location sourceLocation) :
    id{id}, skiplist{[id, sourceLocation] {
        const auto filepath{filepathPrefix + std::to_string(id) + ".db"};

        if (!std::filesystem::exists(filepath)) return Skiplist{};

        std::ifstream file{filepath, std::ios::binary};
        if (!file) {
            throw Exception{
                Log{Log::Level::fatal, "failed to open database file", sourceLocation}
            };
        }

        std::vector<std::byte> buffer{std::filesystem::file_size(filepath)};
        file.read(reinterpret_cast<char *>(buffer.data()), static_cast<long>(buffer.size()));

        return Skiplist{buffer};
    }()} {}

std::unordered_map<unsigned long, Database> Database::databases{initialize()};
