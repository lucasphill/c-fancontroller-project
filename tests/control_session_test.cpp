#include "../src/control_session.h"
#include <iostream>

using namespace fanctl;
namespace {
void expect(bool value,const char* text) {if (!value) throw std::runtime_error(text);}
template<class Function> void fails(Function operation) {
    bool failed=false;try {operation();} catch (const std::exception&) {failed=true;}
    expect(failed,"Expected failure");
}
struct Firmware final : ThermalBackend {
    Snapshot state{160,{{50,1500,0},{51,1700,0}},{{1,60},{6,30}}};
    bool rejectRestore=false,readFailure=false,failDynamicSecond=false;
    int writes=0;
    Snapshot read() override {if (readFailure) throw std::runtime_error("read failed");return state;}
    std::uint32_t readProfile() override {return state.profile;}
    std::uint32_t readBoost(std::uint32_t id) override {return findFan(state,id).boost;}
    void setProfile(std::uint32_t value) override {++writes;state.profile=value;}
    void setBoost(std::uint32_t id,std::uint32_t value) override {
        ++writes;if (rejectRestore && value==0) throw std::runtime_error("restore failed");
        for (auto& fan : state.fans) if (fan.id==id) fan.boost=value;
        if (failDynamicSecond && id==51 && value==65) throw std::runtime_error("setter failed after mutation");
    }
};
struct StorageState {int saves=0,clears=0;bool failSave=false,failClear=false,released=false;};
struct Storage final : SessionResources {
    StorageState& state;
    explicit Storage(StorageState& value) : state(value) {}
    ~Storage() override {state.released=true;}
    void save(const Snapshot&,const std::vector<Adjustment>&) override {
        ++state.saves;if (state.failSave) throw std::runtime_error("journal failed");
    }
    void clear() override {++state.clears;if (state.failClear) throw std::runtime_error("journal locked");}
};
struct Nvidia final : NvidiaTemperatureSource {
    bool& failure;
    explicit Nvidia(bool& value) : failure(value) {}
    int read() override {if (failure) throw std::runtime_error("GPU lost");return 55;}
};
GpuMonitor dell() {return GpuMonitor(GpuSource::Dell,{});}
void tests() {
    {
        Firmware firmware;StorageState storage;auto monitor=dell();
        ControlSession session(firmware,monitor,{{50,30},{51,30}},std::make_unique<Storage>(storage));
        expect(storage.saves==1 && firmware.writes==0,"Wrote before journal was saved");
        session.apply([] {return false;});expect(session.sample().snapshot.profile==0,"Session not in Custom");
        expect(session.restore().verified && storage.clears==1,"Did not clear journal after verified restoration");
        const auto writes=firmware.writes;
        expect(session.restore().verified && writes==firmware.writes,"Confirmed restoration was not idempotent");
    }
    {
        Firmware firmware;StorageState storage;storage.failSave=true;auto monitor=dell();
        fails([&] {ControlSession session(firmware,monitor,{{50,30}},std::make_unique<Storage>(storage));});
        expect(firmware.writes==0 && storage.released,"Journal failure did not release unused lease");
    }
    {
        Firmware firmware;StorageState storage;auto monitor=dell();
        auto session=std::make_unique<ControlSession>(firmware,monitor,std::vector<Adjustment>{{50,30}},std::make_unique<Storage>(storage));
        session->apply([] {return false;});firmware.rejectRestore=true;
        expect(!session->restore().verified && storage.clears==0 && !storage.released,"Failed restoration lost journal or lease");
        firmware.rejectRestore=false;
        expect(session->restore().verified,"Retry did not restore original baseline");
        session.reset();expect(storage.released && firmware.state.profile==160 && firmware.readBoost(50)==0,"Retry did not release after recovery");
    }
    {
        Firmware firmware;StorageState storage;auto monitor=dell();
        ControlSession session(firmware,monitor,{{50,30}},std::make_unique<Storage>(storage));
        session.apply([] {return false;});storage.failClear=true;
        expect(!session.restore().verified,"Journal cleanup failure was hidden");
        storage.failClear=false;expect(session.restore().verified,"Journal cleanup retry failed");
    }
    {
        Firmware firmware;StorageState storage;bool lost=true;
        GpuMonitor monitor(GpuSource::Nvml,[&] {return std::make_unique<Nvidia>(lost);});
        fails([&] {ControlSession session(firmware,monitor,{{50,30}},std::make_unique<Storage>(storage));});
        expect(firmware.writes==0 && storage.saves==0,"Unavailable GPU allowed session writes");
    }
    {
        Firmware firmware;StorageState storage;bool lost=false;
        GpuMonitor monitor(GpuSource::Nvml,[&] {return std::make_unique<Nvidia>(lost);});
        ControlSession session(firmware,monitor,{{50,30}},std::make_unique<Storage>(storage));
        session.apply([] {return false;});lost=true;
        fails([&] {session.sample();});expect(session.restore().verified,"GPU failure prevented rollback");
    }
    {
        Firmware firmware;StorageState storage;auto monitor=dell();
        ControlSession session(firmware,monitor,{{50,30}},std::make_unique<Storage>(storage));
        session.apply([] {return true;});expect(session.restore().verified && firmware.writes==0,"Cancelled startup wrote hardware");
    }
    {
        Firmware firmware;StorageState storage;auto monitor=dell();
        ControlSession session(firmware,monitor,{{50,30},{51,30}},std::make_unique<Storage>(storage));
        session.apply([] {return false;});
        session.update({{50,85},{51,65}},[] {return false;});
        expect(findFan(session.sample().snapshot,50).boost==85,"Dynamic update not tracked");
        session.update({{50,20},{51,20}},[] {return false;});
        expect(findFan(session.sample().snapshot,50).boost==20,"Normal floor 20 rejected by session" );
        expect(storage.saves==1 && storage.clears==0,"Dynamic update replaced original journal");
        expect(session.restore().verified && firmware.state.profile==160 && firmware.readBoost(50)==0 &&
            firmware.readBoost(51)==0,"Dynamic targets lost original baseline");
    }
    {
        Firmware firmware;StorageState storage;auto monitor=dell();
        ControlSession session(firmware,monitor,{{50,30},{51,30}},std::make_unique<Storage>(storage));
        session.apply([] {return false;});firmware.state.fans[0].boost=31;
        const auto writes=firmware.writes;
        fails([&] {session.update({{50,85},{51,65}},[] {return false;});});
        expect(writes==firmware.writes,"External changes were overwritten");
        expect(session.restore().verified,"External change prevented rollback");
    }
    {
        Firmware firmware;StorageState storage;auto monitor=dell();
        ControlSession session(firmware,monitor,{{50,30},{51,30}},std::make_unique<Storage>(storage));
        session.apply([] {return false;});firmware.failDynamicSecond=true;
        fails([&] {session.update({{50,85},{51,65}},[] {return false;});});
        expect(storage.clears==0,"Failed write cleared journal");
        expect(session.restore().verified && firmware.readBoost(50)==0 && firmware.readBoost(51)==0,
            "Partial dynamic write did not restore both fans");
    }
    {
        Firmware firmware;StorageState storage;auto monitor=dell();
        ControlSession session(firmware,monitor,{{50,30},{51,30}},std::make_unique<Storage>(storage));
        session.apply([] {return false;});int calls=0;
        session.update({{50,85},{51,65}},[&] {return ++calls==2;});
        expect(firmware.readBoost(50)==85 && firmware.readBoost(51)==30,"Cancellation did not stop second update");
        expect(session.restore().verified,"Cancelled update did not restore original baseline");
    }
}
}
int main() {
    try {tests();std::cout << "11 shared control session scenarios passed.\n";return 0;}
    catch (const std::exception& error) {std::cerr << error.what()<<'\n';return 1;}
}
