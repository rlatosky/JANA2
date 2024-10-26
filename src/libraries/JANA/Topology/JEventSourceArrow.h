
// Copyright 2020, Jefferson Science Associates, LLC.
// Subject to the terms in the LICENSE file found in the top-level directory.

#pragma once
#include <JANA/Topology/JTriggeredArrow.h>


class JEventSourceArrow : public JTriggeredArrow<JEventSourceArrow> {
private:
    std::vector<JEventSource*> m_sources;
    size_t m_current_source = 0;
    bool m_barrier_active = false;
    JEvent* m_pending_barrier_event = nullptr;

    Place m_input {this, true};
    Place m_output {this, false};

public:
    JEventSourceArrow(std::string name, std::vector<JEventSource*> sources);

    void set_input(JMailbox<JEvent*>* queue) {
        m_input.set_queue(queue);
    }
    void set_input(JEventPool* pool) {
        m_input.set_pool(pool);
    }
    void set_output(JMailbox<JEvent*>* queue) {
        m_output.set_queue(queue);
    }
    void set_output(JEventPool* pool) {
        m_output.set_pool(pool);
    }

    void initialize() final;
    void finalize() final;
    void fire(JEvent* input, OutputData& outputs, size_t& output_count, JArrowMetrics::Status& status);
};

