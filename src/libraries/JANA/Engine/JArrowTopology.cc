
// Copyright 2020, Jefferson Science Associates, LLC.
// Subject to the terms in the LICENSE file found in the top-level directory.


#include "JArrowTopology.h"
#include "JEventProcessorArrow.h"
#include "JEventSourceArrow.h"


JArrowTopology::JArrowTopology() = default;

JArrowTopology::~JArrowTopology() {
    LOG_INFO(m_logger) << "Deleting JArrowTopology" << LOG_END;
    finish();  // In case we stopped() but didn't finish(),
    for (auto arrow : arrows) {
        delete arrow;
    }
    for (auto queue : queues) {
        delete queue;
    }
}

void JArrowTopology::drain() {
    LOG_INFO(m_logger) << "Draining topology" << LOG_END;
    for (auto source : sources) {
        if (source->get_status() == JActivable::Status::Running) {
            running_arrow_count -= 1;
            if (running_arrow_count == 0) {
                LOG_INFO(m_logger) << "All arrows deactivated. Deactivating topology." << LOG_END;
                finish();
            }
            // TODO: A possibly better option is to give each Arrow a reference to running_arrow_count
            //       and only modify running_arrow_count from inside on_status_change.
            // TODO: There is a race condition here where a source can stop on its own in between get_status() and stop(),
            //       causing the running_arrow_count to be wrong and finish() to not be triggered.
            //       This would be basically impossible to detect via testing.
            //       I'm considering creating a single TopologyMutex that controls access to
            //         - scheduler
            //         - arrow thread counts
            //         - activable status
            //         - running upstream counts
        }
        source->stop();
        // We stop (as opposed to finish) for two reasons:
        // 1. There might be workers in the middle of calling eventSource->GetEvent.
        // 2. drain() might be called from a signal handler. It isn't safe to make syscalls during signal handlers
        //    due to risk of deadlock. (We technically shouldn't even do logging!)
    }
}

void JArrowTopology::on_status_change(Status old_status, Status new_status) {
    LOG_INFO(m_logger) << "Topology status change: " << old_status << " => " << new_status << LOG_END;
    if (old_status == Status::Unopened && new_status == Status::Running) {
        for (auto source : sources) {
            // We activate all sources, and everything downstream activates automatically
            source->run();
        }
        metrics.start(0);
    }
    else if (old_status == Status::Running && new_status == Status::Stopped) {
        // This stops all arrows WITHOUT draining queues.
        // There might still be some events being worked on, so the caller to stop() should call wait_until_stopped() afterwards.
        // Note that this won't call finish(), but we are allowed to call finish() later (importantly, after wait_until_stopped)
        for (auto arrow: arrows) {
            arrow->stop(); // If arrow is not running, stop() is a no-op
        }
        metrics.stop();
    }
    else if (old_status == Status::Stopped && new_status == Status::Running) {
        metrics.reset();
        metrics.start(0);
        for (auto source: sources) {
            if (source->get_status() != JActivable::Status::Finished) {
                source->run();
            }
        }
    }
    else if (new_status == Status::Finished) {
        metrics.stop();
        for (auto arrow : arrows) {
            arrow->finish();
        }
    }
}
