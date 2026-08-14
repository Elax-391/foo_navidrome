#include "../ScrobbleLogic.h"
#include "../ScrobbleWorker.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}

bool isSubmission(const std::optional<navidrome::ScrobbleDecision>& decision,
                  navidrome::ScrobbleSubmission expected) {
    return decision && decision->submission == expected;
}

navidrome::ScrobbleWorkItem work(std::uint64_t sessionId, const char* songId) {
    navidrome::ScrobbleWorkItem item;
    item.sessionId = sessionId;
    item.songId = songId;
    item.identity = "server\nuser";
    item.context.serverUrl = "https://music.example";
    item.context.username = "user";
    return item;
}

void testSessionThresholds() {
    navidrome::ScrobbleSession session;
    const auto nowPlaying = session.startTrack("song", 100.0, 0.0, 0.0);
    check(isSubmission(nowPlaying, navidrome::ScrobbleSubmission::NowPlaying),
          "valid track emits now-playing once");
    check(!session.onTime(49.9, 49.9), "below half-duration does not submit");
    check(isSubmission(session.onTime(50.0, 50.0),
                       navidrome::ScrobbleSubmission::Completed),
          "exact half-duration submits");
    check(!session.onTime(80.0, 80.0), "completed session submits once");

    session.startTrack("long", 1000.0, 0.0, 100.0);
    check(!session.onTime(239.9, 339.9), "long track waits below 240 seconds");
    check(isSubmission(session.onTime(240.0, 340.0),
                       navidrome::ScrobbleSubmission::Completed),
          "long track threshold is capped at 240 seconds");

    session.startTrack("unknown", 0.0, 0.0, 400.0);
    check(!session.onTime(239.0, 639.0), "unknown duration waits 240 seconds");
    check(isSubmission(session.onTime(240.0, 640.0),
                       navidrome::ScrobbleSubmission::Completed),
          "unknown duration submits at 240 seconds");
}

void testPauseSeekAndClockJumps() {
    navidrome::ScrobbleSession session;
    session.startTrack("song", 100.0, 0.0, 0.0);
    session.onTime(10.0, 10.0);
    session.onPause(true, 10.0, 10.0);
    check(!session.onTime(90.0, 100.0), "paused playback never accumulates");
    session.onPause(false, 10.0, 110.0);
    session.onTime(20.0, 120.0);
    check(session.listenedSeconds() == 20.0,
          "resume continues from a fresh baseline without pause wall time");

    session.onSeek(90.0, 121.0);
    session.onTime(95.0, 126.0);
    check(session.listenedSeconds() == 25.0,
          "forward seek position is excluded from listened time");
    session.onSeek(5.0, 127.0);
    session.onTime(15.0, 137.0);
    check(session.listenedSeconds() == 35.0,
          "backward seek does not erase or fabricate listened time");

    session.onTime(80.0, 138.0);
    check(session.listenedSeconds() == 35.0,
          "unannounced implausible media jump is rejected");
    session.onTime(81.0, 139.0);
    check(session.listenedSeconds() == 36.0,
          "clock baseline recovers after a rejected jump");
}

void testTrackAndLifecycleBoundaries() {
    navidrome::ScrobbleSession session;
    check(!session.startTrack("", 100.0, 0.0, 0.0),
          "empty song id is ignored");
    session.startTrack("stopped", 100.0, 0.0, 0.5);
    session.onTime(10.0, 10.5);
    check(!session.onStop(navidrome::ScrobbleStopReason::User, 10.0, 10.5) &&
          !session.active(),
          "user stop below threshold ends without a completed submission");
    const auto first = session.startTrack("same", 20.0, 0.0, 1.0);
    session.onTime(5.0, 6.0);
    const auto replay = session.startTrack("same", 20.0, 0.0, 7.0);
    check(first && replay && replay->sessionId > first->sessionId,
          "same URI replay creates a distinct session");
    check(session.listenedSeconds() == 0.0,
          "rapid new track resets previous listened time");

    session.onTime(9.0, 16.0);
    check(isSubmission(session.onStop(
              navidrome::ScrobbleStopReason::EndOfFile, 10.0, 17.0),
              navidrome::ScrobbleSubmission::Completed),
          "stop settles the final plausible playback delta");
    check(!session.onTime(20.0, 27.0), "late time after stop is ignored");

    session.startTrack("disabled", 20.0, 0.0, 30.0);
    session.setEnabled(false);
    check(!session.active() && !session.onTime(20.0, 50.0),
          "disable invalidates the active session immediately");
    check(!session.startTrack("disabled", 20.0, 0.0, 51.0),
          "disabled reducer rejects new sessions");
    session.setEnabled(true);
    check(session.startTrack("enabled", 20.0, 0.0, 52.0).has_value(),
          "re-enable accepts the next track");
    session.shutdown();
    session.shutdown();
    check(!session.enabled() && !session.active() &&
          !session.startTrack("late", 20.0, 0.0, 60.0),
          "shutdown is idempotent and terminal");
}

void testWorkerFifoAndSnapshots() {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<navidrome::ScrobbleWorkItem> executed;
    navidrome::ScrobbleWorker worker([&](const navidrome::ScrobbleWorkItem& item) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            executed.push_back(item);
        }
        changed.notify_all();
    });

    auto first = work(1, "first");
    first.context.username = "account-a";
    first.identity = "server\naccount-a";
    auto second = work(2, "second");
    second.context.username = "account-b";
    second.identity = "server\naccount-b";
    second.submission = true;
    worker.enqueue(first);
    worker.enqueue(second);
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock, std::chrono::seconds(2), [&] {
            return executed.size() == 2;
        });
    }
    worker.shutdown();
    check(executed.size() == 2 && executed[0].songId == "first" &&
          executed[1].songId == "second",
          "worker preserves FIFO order");
    check(executed.size() == 2 &&
          executed[0].context.username == "account-a" &&
          executed[0].identity == "server\naccount-a" &&
          executed[1].context.username == "account-b" &&
          executed[1].identity == "server\naccount-b" &&
          executed[1].submission,
          "worker preserves immutable account snapshots and submission flags");
}

void testWorkerOrderingAndDisable() {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> executed;
    bool firstStarted = false;
    bool firstFinished = false;
    bool releaseFirst = false;

    navidrome::ScrobbleWorker worker([&](const navidrome::ScrobbleWorkItem& item) {
        std::unique_lock<std::mutex> lock(mutex);
        executed.push_back(item.songId);
        if (item.songId == "first") {
            firstStarted = true;
            changed.notify_all();
            changed.wait(lock, [&] { return releaseFirst; });
            firstFinished = true;
            changed.notify_all();
        }
    });

    check(worker.enqueue(work(1, "first")) &&
          worker.enqueue(work(2, "second")) &&
          worker.enqueue(work(3, "third")),
          "enabled worker accepts FIFO items");
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock, std::chrono::seconds(2), [&] { return firstStarted; });
    }
    worker.setEnabled(false);
    {
        std::lock_guard<std::mutex> lock(mutex);
        releaseFirst = true;
    }
    changed.notify_all();
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock, std::chrono::seconds(2), [&] { return firstFinished; });
    }
    worker.setEnabled(true);
    check(worker.enqueue(work(4, "fourth")),
          "worker accepts new work after re-enable");
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait_for(lock, std::chrono::seconds(2), [&] {
            return executed.size() == 2;
        });
    }
    worker.shutdown();
    worker.shutdown();
    check(executed == std::vector<std::string>({"first", "fourth"}),
          "disable keeps in-flight work, drops queued work and permits re-enable");
    check(!worker.enqueue(work(4, "late")),
          "shutdown rejects later work");
}

void testWorkerFailureHasNoRetry() {
    std::atomic<int> attempts{0};
    {
        navidrome::ScrobbleWorker worker([&](const navidrome::ScrobbleWorkItem&) {
            ++attempts;
            throw std::runtime_error("ambiguous transport failure");
        });
        worker.enqueue(work(1, "once"));
        for (int i = 0; i < 100 && attempts.load() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        worker.shutdown();
    }
    check(attempts.load() == 1, "ambiguous worker failure is never retried");
}

} // namespace

int main() {
    std::cout << "ScrobbleTests starting\n";
    testSessionThresholds();
    testPauseSeekAndClockJumps();
    testTrackAndLifecycleBoundaries();
    testWorkerFifoAndSnapshots();
    testWorkerOrderingAndDisable();
    testWorkerFailureHasNoRetry();
    if (failures != 0) return 1;
    std::cout << "All scrobble tests passed\n";
    return 0;
}
