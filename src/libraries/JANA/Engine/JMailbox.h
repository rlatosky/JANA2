
// Copyright 2020, Jefferson Science Associates, LLC.
// Subject to the terms in the LICENSE file found in the top-level directory.


#ifndef JANA2_JMAILBOX_H
#define JANA2_JMAILBOX_H

#include <queue>
#include <mutex>
#include <JANA/Utils/JCpuInfo.h>
#include <JANA/Services/JLoggingService.h>

/// JMailbox is a threadsafe event queue designed for communication between Arrows.
/// It is different from the standard data structure in the following ways:
///   - pushes and pops return a Status enum, handling the problem of .size() always being stale
///   - pops may fail but pushes may not
///   - pushes and pops are chunked, reducing contention and handling the failure case cleanly
///   - when the .reserve() method is used, the queue size is bounded
///   - the underlying queue may be shared by all threads, NUMA-domain-local, or thread-local
///   - the Arrow doesn't have to know anything about locality.
///
/// To handle memory locality at different granularities, we introduce the concept of a domain.
/// Each thread belongs to exactly one domain. Domains are represented by contiguous unsigned
/// ints starting at 0. While JArrows are wired to one logical JMailbox, JWorkers interact with
/// the physical DomainLocalMailbox corresponding to their very own memory domain.
///
/// \tparam T must be moveable. Usually this is unique_ptr<JEvent>.
///
/// Improvements:
///   1. Pad DomainLocalMailbox
///   2. Enable work stealing
///   3. Triple mutex trick to give push() priority?


class JQueue {
protected:
    size_t m_threshold;
    size_t m_locations_count;
    bool m_enable_work_stealing = false;

public:
    inline size_t get_threshold() { return m_threshold; }
    inline size_t get_locations_count() { return m_locations_count; }
    inline bool is_work_stealing_enabled() { return m_enable_work_stealing; }


    inline JQueue(size_t threshold, size_t locations_count, bool enable_work_stealing)
        : m_threshold(threshold), m_locations_count(locations_count), m_enable_work_stealing(enable_work_stealing) {}
    virtual ~JQueue() = default;
};

template <typename T>
class JMailbox : public JQueue {

    struct LocalMailbox {
        std::mutex mutex;
        std::deque<T> queue;
        size_t reserved_count = 0;
    };

    // TODO: Copy these params into DLMB for better locality
    std::unique_ptr<LocalMailbox[]> m_mailboxes;

public:

    enum class Status {Ready, Congested, Empty, Full};

    friend std::ostream& operator<<(std::ostream& os, const Status& s) {
        switch (s) {
            case Status::Ready:     os << "Ready"; break;
            case Status::Congested: os << "Congested"; break;
            case Status::Empty:     os << "Empty"; break;
            case Status::Full:      os << "Full"; break;
            default:                os << "Unknown"; break;
        }
        return os;
    }


    /// threshold: the (soft) maximum number of items in the queue at any time
    /// domain_count: the number of domains
    /// enable_work_stealing: allow domains to pop from other domains' queues when theirs is empty
    JMailbox(size_t threshold=100, size_t locations_count=1, bool enable_work_stealing=false)
        : JQueue(threshold, locations_count, enable_work_stealing) {

        m_mailboxes = std::unique_ptr<LocalMailbox[]>(new LocalMailbox[locations_count]);
    }

    virtual ~JMailbox() {
        //delete [] m_mailboxes;
    }

    // We can do this (for now) because we use a deque underneath, so threshold is 'soft'
    inline void set_threshold(size_t threshold) { m_threshold = threshold; }

    /// size() counts the number of items in the queue across all domains
    /// This should be used sparingly because it will mess up a bunch of caches.
    /// Meant to be used by measure_perf()
    size_t size() {
        size_t result = 0;
        for (size_t i = 0; i<m_locations_count; ++i) {
            std::lock_guard<std::mutex> lock(m_mailboxes[i].mutex);
            result += m_mailboxes[i].queue.size();
        }
        return result;
    };

    /// size(domain) counts the number of items in the queue for a particular domain
    /// Meant to be used by Scheduler::next_assignment() and measure_perf(), eventually
    size_t size(size_t domain) {
        return m_mailboxes[domain].queue.size();
    }

    /// reserve(requested_count) keeps our queues bounded in size. The caller should
    /// reserve their desired chunk size on the output queue first. The output
    /// queue will return a reservation which is less than or equal to requested_count.
    /// The caller may then request as many items from the input queue as have been
    /// reserved on the output queue. Note that because the input queue may return
    /// fewer items than requested, the caller must push their original reserved_count
    /// alongside the items, to avoid a "reservation leak".
    size_t reserve(size_t requested_count, size_t domain = 0) {

        LocalMailbox& mb = m_mailboxes[domain];
        std::lock_guard<std::mutex> lock(mb.mutex);
        size_t doable_count = m_threshold - mb.queue.size() - mb.reserved_count;
        if (doable_count > 0) {
            size_t reservation = std::min(doable_count, requested_count);
            mb.reserved_count += reservation;
            return reservation;
        }
        return 0;
    };

    /// push(items, reserved_count, domain) This function will always
    /// succeed, although it may exceed the threshold if the caller didn't reserve
    /// space, and it may take a long time because it will wait on a mutex.
    /// Note that if the caller had called reserve(), they must pass in the reserved_count here.
    Status push(std::vector<T>& buffer, size_t reserved_count = 0, size_t domain = 0) {

        auto& mb = m_mailboxes[domain];
        std::lock_guard<std::mutex> lock(mb.mutex);
        mb.reserved_count -= reserved_count;
        for (const T& t : buffer) {
             mb.queue.push_back(std::move(t));
        }
        buffer.clear();
        if (mb.queue.size() > m_threshold) {
            return Status::Full;
        }
        return Status::Ready;
    }

    Status push(T& item, size_t reserved_count = 0, size_t domain = 0) {

        auto& mb = m_mailboxes[domain];
        std::lock_guard<std::mutex> lock(mb.mutex);
        mb.reserved_count -= reserved_count;
        mb.queue.push_back(std::move(item));
        size_t size = mb.queue.size();
        if (size > m_threshold) {
            return Status::Full;
        }
        return Status::Ready;
    }


    /// pop() will pop up to requested_count items for the desired domain.
    /// If many threads are contending for the queue, this will fail with Status::Contention,
    /// in which case the caller should probably consult the Scheduler.
    Status pop(std::vector<T>& buffer, size_t requested_count, size_t location_id = 0) {

        auto& mb = m_mailboxes[location_id];
        if (!mb.mutex.try_lock()) {
            return Status::Congested;
        }
        auto nitems = std::min(requested_count, mb.queue.size());
        buffer.reserve(nitems);
        for (size_t i=0; i<nitems; ++i) {
            buffer.push_back(std::move(mb.queue.front()));
            mb.queue.pop_front();
        }
        auto size = mb.queue.size();
        mb.mutex.unlock();
        if (size >= m_threshold) {
            return Status::Full;
        }
        else if (size != 0) {
            return Status::Ready;
        }
        return Status::Empty;
    }


    Status pop(T& item, bool& success, size_t location_id = 0) {

        success = false;
        auto& mb = m_mailboxes[location_id];
        if (!mb.mutex.try_lock()) {
            return Status::Congested;
        }
        size_t nitems = mb.queue.size();
        if (nitems > 1) {
            item = std::move(mb.queue.front());
            mb.queue.pop_front();
            success = true;
            mb.mutex.unlock();
            return Status::Ready;
        }
        else if (nitems == 1) {
            item = std::move(mb.queue.front());
            mb.queue.pop_front();
            success = true;
            mb.mutex.unlock();
            return Status::Empty;
        }
        mb.mutex.unlock();
        return Status::Empty;
    }




    bool try_push(T* buffer, size_t count, size_t domain = 0) {
        auto& mb = m_mailboxes[domain];
        std::lock_guard<std::mutex> lock(mb.mutex);
        if (mb.queue.size() + count > m_threshold) return false;
        for (size_t i=0; i<count; ++i) {
             mb.queue.push_back(buffer[i]);
             buffer[i] = nullptr;
        }
        return true;
    }

    void push_and_unreserve(T* buffer, size_t count, size_t reserved_count = 0, size_t domain = 0) {

        auto& mb = m_mailboxes[domain];
        std::lock_guard<std::mutex> lock(mb.mutex);
        mb.reserved_count -= reserved_count;
        assert(mb.queue.size() + count <= m_threshold);
        for (size_t i=0; i<count; ++i) {
             mb.queue.push_back(buffer[i]);
             buffer[i] = nullptr;
        }
    }

    size_t pop(T* buffer, size_t min_requested_count, size_t max_requested_count, size_t location_id = 0) {

        auto& mb = m_mailboxes[location_id];
        std::lock_guard<std::mutex> lock(mb.mutex);

        if (mb.queue.size() < min_requested_count) return 0;

        auto nitems = std::min(max_requested_count, mb.queue.size());

        for (size_t i=0; i<nitems; ++i) {
            buffer[i] = mb.queue.front();
            mb.queue.pop_front();
        }
        return nitems;
    }

    size_t pop_and_reserve(T* buffer, size_t min_requested_count, size_t max_requested_count, size_t location_id = 0) {

        auto& mb = m_mailboxes[location_id];
        std::lock_guard<std::mutex> lock(mb.mutex);

        if (mb.queue.size() < min_requested_count) return 0;

        auto nitems = std::min(max_requested_count, mb.queue.size());
        mb.reserved_count += nitems;

        for (size_t i=0; i<nitems; ++i) {
            buffer[i] = mb.queue.front();
            mb.queue.pop_front();
        }
        return nitems;
    }

    size_t reserve(size_t min_requested_count, size_t max_requested_count, size_t domain) {

        LocalMailbox& mb = m_mailboxes[domain];
        std::lock_guard<std::mutex> lock(mb.mutex);
        size_t available_count = m_threshold - mb.queue.size() - mb.reserved_count;
        size_t count = std::min(available_count, max_requested_count);
        if (count < min_requested_count) {
            return 0;
        }
        mb.reserved_count += count;
        return count;
    };

};



#endif //JANA2_JMAILBOX_H
