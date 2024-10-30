
#include "JANA/Engine/JExecutionEngine.h"
#include "JANA/Utils/JBenchUtils.h"
#include <catch.hpp>

#include <JANA/JApplication.h>
#include <JANA/JEventSource.h>
#include <JANA/JEventProcessor.h>

namespace jana::engine::tests {

struct TestData { int x; };
struct TestSource : public JEventSource {
    JBenchUtils bench;
    TestSource() {
        SetCallbackStyle(CallbackStyle::ExpertMode);
    }
    Result Emit(JEvent& event) override {
        event.Insert<TestData>(new TestData {.x=(int) GetEmittedEventCount() * 2}, "src");
        bench.consume_cpu_ms(100);
        return Result::Success;
    }
};
struct TestProc : public JEventProcessor {
    JBenchUtils bench;
    TestProc() {
        SetCallbackStyle(CallbackStyle::ExpertMode);
    }
    void ProcessParallel(const JEvent& event) override {
        bench.consume_cpu_ms(300);
        auto src_x = event.Get<TestData>("src").at(0)->x;
        event.Insert<TestData>(new TestData {.x=src_x + 1}, "map");
    }
    void Process(const JEvent& event) override {
        bench.consume_cpu_ms(200);
        auto map_x = event.Get<TestData>("map").at(0)->x;
        REQUIRE(map_x == (int)event.GetEventNumber()*2 + 1);
    }
};


TEST_CASE("JExecutionEngine_NoWorkers") {

    JApplication app;
    app.Add(new TestSource());
    app.Add(new TestProc());
    app.ProvideService(std::make_shared<JExecutionEngine>());
    app.Initialize();
    auto sut = app.GetService<JExecutionEngine>();

    SECTION("StateMachine_ManualFinish") {
        REQUIRE(sut->GetRunStatus() == JExecutionEngine::RunStatus::Paused);
        sut->Scale(0);
        sut->Run();
        REQUIRE(sut->GetRunStatus() == JExecutionEngine::RunStatus::Running);
        sut->RequestPause();
        REQUIRE(sut->GetRunStatus() == JExecutionEngine::RunStatus::Pausing);
        sut->Wait();
        REQUIRE(sut->GetRunStatus() == JExecutionEngine::RunStatus::Paused);
        sut->Finish();
        REQUIRE(sut->GetRunStatus() == JExecutionEngine::RunStatus::Finished);
    }

    SECTION("StateMachine_AutoFinish") {
        REQUIRE(sut->GetRunStatus() == JExecutionEngine::RunStatus::Paused);
        sut->Scale(0);
        sut->Run();
        REQUIRE(sut->GetRunStatus() == JExecutionEngine::RunStatus::Running);
        sut->RequestPause();
        REQUIRE(sut->GetRunStatus() == JExecutionEngine::RunStatus::Pausing);
        sut->Wait(true);
        REQUIRE(sut->GetRunStatus() == JExecutionEngine::RunStatus::Finished);
    }
}

TEST_CASE("JExecutionEngine_NoWorkers_Autofinish") {


}



} // jana::engine::tests
