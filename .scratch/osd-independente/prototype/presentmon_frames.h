#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace fanctl::prototype {
struct FramesSnapshot {
    std::optional<double> fps, frameTimeMs;
    std::wstring status=L"Coleta de quadros parada";
    std::uint64_t eventsLost=0;
};
// Prototype only. Owns one uniquely named ETW session; never takes over another session.
class PresentMonFrames {
public:
    PresentMonFrames();
    ~PresentMonFrames();
    PresentMonFrames(const PresentMonFrames&)=delete;
    PresentMonFrames& operator=(const PresentMonFrames&)=delete;
    void start(DWORD pid);
    void stop();
    FramesSnapshot snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
