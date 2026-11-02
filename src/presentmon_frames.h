#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace fanctl {
struct FramesSnapshot {
    std::optional<double> fps,frameTimeMs;
    std::wstring status=L"Medição de quadros desligada";
    std::uint64_t eventsLost=0;
    DWORD processId=0;
};

// setTarget/snapshot only touch a short-lived cache lock. ETW acquisition, draining,
// cleanup and joins belong to the service thread. stop is application teardown.
class PresentMonFrames {
public:
    PresentMonFrames();
    ~PresentMonFrames();
    PresentMonFrames(const PresentMonFrames&)=delete;
    PresentMonFrames& operator=(const PresentMonFrames&)=delete;
    void setTarget(DWORD processId);
    void start(DWORD processId){setTarget(processId);}
    void retry();
    FramesSnapshot snapshot()const;
    void stop();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
