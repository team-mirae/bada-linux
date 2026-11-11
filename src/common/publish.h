// The state for other programs (docs/api.md): a JSON file rewritten atomically, and a D-Bus signal
// on the session bus with the same JSON, each only when the JSON changed.
#pragma once

#include <filesystem>
#include <string>

#include "session.h"

namespace bl {

class Publisher final : public StateSink {
public:
    explicit Publisher(std::string id);
    ~Publisher() override;
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;

    void Publish(const std::string& json) override;
    void Clear() override;

    // $XDG_RUNTIME_DIR/bada-linux/<id>/state.json (else /tmp/bada-linux-<uid>/<id>/state.json).
    static std::filesystem::path StatePath(const std::string& id);

private:
    void Signal(const std::string& json);

    std::string id_;
    std::filesystem::path path_;
    std::string last_;
    void* bus_ = nullptr;  // DBusConnection*, private; null when there's no session bus
    bool bus_tried_ = false;
};

}  // namespace bl
