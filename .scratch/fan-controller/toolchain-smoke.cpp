#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wbemidl.h>
#include <cstdio>

int main() {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(result)) {
        std::printf("COM initialization failed: 0x%08lX\n", static_cast<unsigned long>(result));
        return 1;
    }
    IWbemLocator* locator = nullptr;
    const HRESULT locatorResult = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(&locator));
    if (locator) locator->Release();
    CoUninitialize();
    if (FAILED(locatorResult)) {
        std::printf("WMI locator creation failed: 0x%08lX\n", static_cast<unsigned long>(locatorResult));
        return 1;
    }
    std::puts("MSVC + Windows SDK + COM/WMI: OK (no hardware queries)");
    return 0;
}
