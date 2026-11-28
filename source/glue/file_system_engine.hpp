#pragma once
// Generic native file-system engine. C++17, read-only source filesystem access.
// JSON returned by this interface is UTF-8. The engine has no UI or DOM dependency.
#include <memory>
#include <string>

namespace BOSS::FileSystem {
class Engine {
public:
    // Mutating public calls are serialized by the owning session/control thread.
    // Scan/image/output workers only mutate their own protected engine state.
    // assetPrefix is a relative resource prefix; its trailing '/' is normalized.
    Engine(std::string cacheDirectory, std::string assetPrefix);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    static std::string Capabilities();
    static std::string Drives();
    static std::string PickDirectory(); // Empty string means cancelled/unsupported.
    std::string Start(const std::string& rootUtf8);
    std::string Control(const std::string& action);
    std::string Poll() const;
    std::string Items(const std::string& mode, int offset, int limit) const;
    std::string Item(const std::string& id) const;
    std::string Issues(int offset, int limit) const;
    std::string Preview(const std::string& id);
    std::string PreviewPoll() const;
    std::string PreviewRelease();
    std::string Reveal(const std::string& id) const;
    std::string SetRules(int unchangedDays, double minimumGiB);
    std::string SaveSnapshot();
    std::string Snapshots() const;
    std::string LoadSnapshot(const std::string& name);
    std::string ExportReport();
    std::string TaskPoll() const;
    std::string TaskCancel();

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace BOSS::FileSystem
