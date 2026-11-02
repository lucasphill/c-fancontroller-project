#include "../src/boost_session.h"
#include <iostream>
#include <map>

using namespace fanctl;
namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void expectFailure(F operation) {
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    expect(failed, "Expected operation to fail");
}
class Firmware : public ThermalBackend {
public:
    Snapshot state{160, {{50, 1500, 0}, {51, 1700, 0}}, {{1, 70}, {6, 57}}};
    std::vector<Adjustment> writes;
    bool failSnapshot = false;
    bool failSecondWriteAfterMutation = false;
    bool rejectReadback = false;
    bool failRestore50 = false;
    bool failEnterAfterMutation = false;
    bool rejectCustom = false;
    bool failProfileRestore = false;
    bool resetBoostsOnProfile = false;
    std::vector<std::string> operations;
    Snapshot read() override {
        if (failSnapshot) throw std::runtime_error("sensor unavailable");
        return state;
    }
    std::uint32_t readProfile() override { return state.profile; }
    std::uint32_t readBoost(std::uint32_t id) override { return findFan(state, id).boost; }
    void setProfile(std::uint32_t value) override {
        operations.push_back("profile=" + std::to_string(value));
        if (failProfileRestore && value == 160) throw std::runtime_error("profile restore rejected");
        if (rejectCustom && value == 0) return;
        state.profile = value;
        if (resetBoostsOnProfile) for (auto& fan : state.fans) fan.boost = 7;
        if (failEnterAfterMutation && value == 0) throw std::runtime_error("profile applied but response lost");
    }
    void setBoost(std::uint32_t id, std::uint32_t value) override {
        operations.push_back("fan=" + std::to_string(id));
        writes.push_back({id, value});
        if (failRestore50 && id == 50 && value == 0) throw std::runtime_error("restore rejected");
        for (auto& fan : state.fans) if (fan.id == id) {
            if (!rejectReadback || value == 0) fan.boost = value;
        }
        if (failSecondWriteAfterMutation && id == 51 && value != 0)
            throw std::runtime_error("response lost after firmware applied");
    }
};
void runTests() {
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}});
        session.apply([] { return false; });
        expect(firmware.readProfile() == 0, "Custom was not selected before boost");
        expect(session.sample().fans[0].boost == 30, "Boost was not applied");
        expect(firmware.readBoost(51) == 0, "Omitted fan was modified");
        expect(session.restore().verified, "Normal restoration failed");
        expect(firmware.readBoost(50) == 0, "Original boost was not restored");
    }
    {
        Firmware firmware;
        firmware.failSnapshot = true;
        expectFailure([&] { BoostSession session(firmware, {{50, 30}}); });
        expect(firmware.writes.empty(), "Wrote without original snapshot");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}, {51, 30}});
        firmware.failSecondWriteAfterMutation = true;
        expectFailure([&] { session.apply([] { return false; }); });
        expect(session.restore().verified, "Partial failure did not restore both fans");
        expect(firmware.readBoost(50) == 0 && firmware.readBoost(51) == 0,
            "A setter that threw after mutation was not restored");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}});
        firmware.rejectReadback = true;
        expectFailure([&] { session.apply([] { return false; }); });
        expect(session.restore().verified, "Readback mismatch did not restore");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}});
        session.apply([] { return false; });
        firmware.failSnapshot = true;
        expectFailure([&] { session.sample(); });
        expect(session.restore().verified, "Sensor failure prevented boost restoration");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}, {51, 30}});
        session.apply([] { return false; });
        firmware.failRestore50 = true;
        expect(!session.restore().verified, "Restoration failure was reported as success");
        expect(firmware.readBoost(51) == 0, "One restoration failure blocked another fan");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}});
        session.apply([] { return false; });
        firmware.state.profile = 161;
        expectFailure([&] { session.sample(); });
        expect(session.restore().verified, "Original profile was not restored after external change");
        expect(firmware.readProfile() == 160, "Profile restore failed");
        expect(firmware.readBoost(50) == 0, "Profile change prevented restoring touched boost");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}, {51, 30}});
        int checks = 0;
        session.apply([&] { return ++checks > 2; });
        expect(firmware.readBoost(50) == 30 && firmware.readBoost(51) == 0,
            "Cancellation between fans was ignored");
        expect(session.restore().verified, "Cancelled application did not restore");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}});
        firmware.state.fans[1].boost = 10;
        expectFailure([&] { session.apply([] { return false; }); });
        expect(firmware.writes.empty(), "Original-state drift did not prevent writes");
    }
    {
        Firmware firmware;
        expectFailure([&] { BoostSession session(firmware, {{50, 101}}); });
        expectFailure([&] { BoostSession session(firmware, {{50, 20}, {50, 30}}); });
        expectFailure([&] { BoostSession session(firmware, {{52, 30}}); });
        expect(firmware.writes.empty(), "Invalid requests caused writes");
    }
    {
        Firmware firmware;
        firmware.resetBoostsOnProfile = true;
        firmware.state.fans[1].boost = 12;
        BoostSession session(firmware, {{50, 30}});
        session.apply([] { return false; });
        expect(firmware.readBoost(51) == 12, "Profile switch lost omitted fan setting");
        expect(firmware.operations.front() == "profile=0", "Boost written before Custom");
        const auto restoreStart = firmware.operations.size();
        expect(session.restore().verified, "Profile reset corrupted restored boosts");
        expect(firmware.operations[restoreStart] == "profile=160", "Restore did not select original profile first");
        expect(firmware.readBoost(50) == 0 && firmware.readBoost(51) == 12, "Original fan values not restored after profile reset");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}});
        firmware.failEnterAfterMutation = true;
        expectFailure([&] { session.apply([] { return false; }); });
        expect(session.restore().verified, "Throwing profile setter was not rolled back");
        expect(firmware.readProfile() == 160, "Original profile lost");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}});
        firmware.rejectCustom = true;
        expectFailure([&] { session.apply([] { return false; }); });
        expect(firmware.writes.empty(), "Wrote boost despite Custom not confirmed");
        expect(session.restore().verified, "Rejected Custom did not preserve original state");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}, {51, 30}});
        session.apply([] { return false; });
        firmware.failProfileRestore = true;
        expect(!session.restore().verified, "Failed profile restore declared success");
        expect(firmware.readBoost(50) == 0 && firmware.readBoost(51) == 0, "Profile restore error blocked fan restores");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}});
        int checks = 0;
        session.apply([&] { return ++checks > 1; });
        expect(firmware.readProfile() == 0 && firmware.writes.empty(), "Cancellation after profile did not prevent fan writes");
        expect(session.restore().verified && firmware.readProfile() == 160, "Cancellation after profile did not restore it");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 30}});
        session.apply([] { return true; });
        expect(firmware.operations.empty(), "Cancellation before profile still wrote");
        expect(session.restore().verified, "No-op cancellation lost original state");
    }
    {
        Firmware firmware;
        firmware.state.profile = 0;
        firmware.state.fans[0].boost = 15;
        firmware.state.fans[1].boost = 20;
        firmware.resetBoostsOnProfile = true;
        BoostSession session(firmware, {{50, 30}});
        session.apply([] { return false; });
        expect(firmware.readBoost(51) == 20, "Already Custom changed omitted fan");
        expect(session.restore().verified, "Already Custom was not restored");
        expect(firmware.readProfile() == 0 && firmware.readBoost(50) == 15 && firmware.readBoost(51) == 20,
            "Nonzero original Custom settings not restored");
    }
    {
        Firmware firmware;
        BoostSession session(firmware, {{50, 100}, {51, 100}});
        session.apply([] { return false; });
        expect(session.sample().fans[0].boost == 100 && firmware.readBoost(51) == 100,
            "Maximum requested boost was not applied");
        expect(session.restore().verified, "Maximum boost did not restore original state");
    }
}
}
int main() {
    try { runTests(); std::cout << "18 boost session scenarios passed (no hardware access).\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
