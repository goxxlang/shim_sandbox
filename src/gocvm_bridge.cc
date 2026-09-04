// Wires wasigoc's GocVM dispatch gate (wasigo::gocvm, runtime.hpp) to the
// real backends already built for the 8 stub topics (src/sapi/handle.cc
// -> src/sapi/real_win.cc). Thin adapter only -- no logic duplicated.
//
// Registration is explicit, not static-init-across-a-static-library-
// archive (a linker can drop an unreferenced .o from a .a with no
// --whole-archive, which this repo's native build doesn't use): wasigoc's
// generated `main()` always calls wasigo::set_os_args() first, and that
// function calls wasigo_gocvm_install_bridge() when compiled with
// -DWASIGO_GOCVM_BRIDGE=1 (goclang++.bat --shim-sandbox sets this).
//
// Async, not the original synchronous adapter: wasigoc now always
// compiles `gocvm.Call(...)` to `co_await wasigo::gocvm::CallAsync(...)`
// (see runtime.hpp's CallAsync doc comment), which needs an
// AsyncHostBridge, not the old HostBridge -- a synchronous bridge blocks
// wasigo's entire single-threaded cooperative scheduler for as long as
// the real Win32/Winsock call takes, so one goroutine's slow I/O (a
// socket recv() with no data yet, a subprocess that hasn't exited)
// stalled every other ready goroutine too.
//
// A small fixed-size worker pool runs W2gSapiHandle, not just one
// thread: a single worker deadlocks the instant two blocking calls on
// two different handles need to be in flight at once -- e.g. os/exec's
// stdin-write pump and stdout-read pump on the same running process
// (found by actually hitting it: a real ExecStdoutRead ReadFile blocked
// waiting for output the child would only produce after consuming stdin
// that ExecStdinWrite's WriteFile, queued behind it, never got to run).
// handles.h's Alloc*/Lookup*/Release now take a real mutex around the
// map operation itself to make this safe -- see that file's doc comment
// for exactly what is and isn't protected.
#include "runtime.hpp"
#include "w2g/sapi.h"

#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kWorkerCount = 4;

struct Job {
  uint64_t id = 0;
  std::string topic;
  std::string payload;
};

class AsyncSapiBridge : public wasigo::gocvm::AsyncHostBridge {
 public:
  AsyncSapiBridge() {
    for (int i = 0; i < kWorkerCount; ++i) {
      workers_.emplace_back(&AsyncSapiBridge::WorkerLoop, this);
    }
  }
  ~AsyncSapiBridge() override {
    {
      std::lock_guard<std::mutex> lk(mu_);
      shutdown_ = true;
    }
    cv_jobs_.notify_all();
    for (auto& w : workers_) {
      if (w.joinable()) w.join();
    }
  }
  AsyncSapiBridge(const AsyncSapiBridge&) = delete;
  AsyncSapiBridge& operator=(const AsyncSapiBridge&) = delete;

  uint64_t Submit(const std::string& topic, const std::string& payload) override {
    uint64_t id;
    {
      std::lock_guard<std::mutex> lk(mu_);
      id = ++next_id_;
      jobs_.push_back(Job{id, topic, payload});
    }
    cv_jobs_.notify_one();
    return id;
  }

  bool PollOne(Completion* out) override {
    std::lock_guard<std::mutex> lk(mu_);
    if (results_.empty()) return false;
    *out = std::move(results_.front());
    results_.pop_front();
    return true;
  }

  void WaitOne(Completion* out) override {
    std::unique_lock<std::mutex> lk(mu_);
    cv_results_.wait(lk, [this] { return !results_.empty(); });
    *out = std::move(results_.front());
    results_.pop_front();
  }

 private:
  void WorkerLoop() {
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lk(mu_);
        cv_jobs_.wait(lk, [this] { return shutdown_ || !jobs_.empty(); });
        if (jobs_.empty()) {
          if (shutdown_) return;
          continue;
        }
        job = std::move(jobs_.front());
        jobs_.pop_front();
      }

      // The actual (potentially blocking) syscall -- runs on this
      // worker thread, off the cooperative scheduler's own thread,
      // which is the entire point.
      Completion c;
      c.id = job.id;
      // Lets gocvm map this call's VThread to the real OS thread that
      // served it (wasigo::gocvm::OSThreadFor / VThread::os_thread) --
      // now genuinely one of kWorkerCount threads, not always the same
      // one; the plumbing already didn't care how many there are.
      c.worker_thread = std::this_thread::get_id();
      char reply_topic[W2G_SAPI_TOPIC_MAX] = {};
      std::vector<uint8_t> buf(1 << 20);
      uint32_t n = static_cast<uint32_t>(buf.size());
      W2gResult rc = W2gSapiHandle(job.topic.c_str(),
                                    reinterpret_cast<const uint8_t*>(job.payload.data()),
                                    static_cast<uint32_t>(job.payload.size()), reply_topic,
                                    sizeof(reply_topic), buf.data(), &n);
      std::string text(reinterpret_cast<char*>(buf.data()), n);
      if (rc == W2G_RESULT_NOT_FOUND) {
        c.ok = false;
        c.err = "unknown gocvm topic '" + job.topic + "'";
      } else if (rc == W2G_RESULT_OK) {
        c.ok = true;
        c.reply = std::move(text);
      } else {
        // UNIMPLEMENTED / INVALID_ARGUMENT / etc: still a real,
        // definitive answer (System-disabled fallback, tls.dial's
        // real-TCP-but-no-handshake note, a malformed payload) -- surface
        // it as the error, never silently as success.
        c.ok = false;
        c.err = text;
      }

      {
        std::lock_guard<std::mutex> lk(mu_);
        results_.push_back(std::move(c));
      }
      cv_results_.notify_one();
    }
  }

  std::mutex mu_;
  std::condition_variable cv_jobs_;
  std::condition_variable cv_results_;
  std::deque<Job> jobs_;
  std::deque<Completion> results_;
  uint64_t next_id_ = 0;
  bool shutdown_ = false;
  std::vector<std::thread> workers_;
};

}  // namespace

extern "C" void wasigo_gocvm_install_bridge() {
  static AsyncSapiBridge bridge;
  wasigo::gocvm::RegisterAsyncHostBridge(&bridge);
}
