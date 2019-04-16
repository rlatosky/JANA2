
#include <JANA/JLogger.h>
#include <JANA/JTopology.h>
#include <JANA/JWorker.h>
#include <JANA/JServiceLocator.h>
#include <greenfield/ExampleComponents.h>
#include <greenfield/LinearTopologyBuilder.h>
#include <greenfield/PerfTestTopology.h>

using namespace std;


int main() {

    serviceLocator = new JServiceLocator();

    auto loggingService = new JLoggingService;
    serviceLocator->provide(loggingService);

    loggingService->set_level("JThreadTeam", JLogLevel::OFF);
    loggingService->set_level("JScheduler", JLogLevel::OFF);
    loggingService->set_level("JTopology", JLogLevel::INFO);

    auto params = new ParameterManager;
    serviceLocator->provide(params);

    params->chunksize = 10;
    params->backoff_tries = 4;
    params->backoff_time = std::chrono::microseconds(50);
    params->checkin_time = std::chrono::milliseconds(400);

    PerfTestSource parse;
    PerfTestMapper disentangle;
    PerfTestMapper track;
    PerfTestReducer plot;

    parse.message_count_limit = 5000;
    parse.latency_ms = 10;
    disentangle.latency_ms = 10;
    track.latency_ms = 10;
    plot.latency_ms = 10;

//    5Hz for full reconstruction
//    20kHz for stripped-down reconstruction (not per-core)
//    Base memory allcation: 100 MB/core + 600MB
//    1 thread/event, disentangle 1 event, turn into 40.
//    disentangled (single event size) : 12.5 kB / event (before blown up)
//    entangled "block of 40": dis * 40
//

    JTopology topology;

    LinearTopologyBuilder builder(topology);

    builder.addSource("parse", parse);
    builder.addProcessor("disentangle", disentangle);
    builder.addProcessor("track", track);
    builder.addSink("plot", plot);

    topology.activate("parse");
    //topology.log_status();
    topology.run(4);

    while (topology.is_active()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        std::cout << "\033[2J";
        topology.log_status();
    }

    topology.wait_until_finished();
    topology.log_status();
}



