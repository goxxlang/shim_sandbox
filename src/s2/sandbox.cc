#include "w2g/s2/sandbox.h"
#include "w2g/system_policy.h"

#include "os.h"

namespace w2g {
namespace s2 {

Sandbox::Sandbox(std::unique_ptr<Executor> executor, std::unique_ptr<Policy> policy)
    : executor_(std::move(executor)), policy_(std::move(policy)) {}

Sandbox::~Sandbox() {
  if (started_ && result_.final_status() == Result::kUnset) Kill();
}

Comms* Sandbox::comms() {
  return executor_ ? executor_->comms() : nullptr;
}

bool Sandbox::RunAsync() {
  if (!executor_ || !policy_) {
    result_ = Result::Setup("nil executor/policy");
    return false;
  }
#if !W2G_ABAC_SYSTEM
  result_ = Result::Setup(kSystemDisabled);
  return false;
#endif
  if (!policy_->abac().rules().empty() &&
      !policy_->CheckShim("s2", w2g::abac::kSpawn, executor_->path_)) {
    result_ = Result::Setup("abac deny s2.Spawn");
    return false;
  }
  os::SpawnReq req;
  req.path = executor_->path_;
  req.argv = executor_->argv_;
  req.env = executor_->env_;
  req.cwd = executor_->cwd_;
  req.child_r = executor_->child_r_.get();
  req.child_w = executor_->child_w_.get();
  req.limits = executor_->limits();
  req.policy = policy_.get();
  os::Spawned sp;
  result_ = os::Spawn(req, &sp);
  if (result_.final_status() != Result::kOk) return false;
  process_ = std::move(sp.process);
  job_ = std::move(sp.job);
  pid_ = sp.pid;
  executor_->child_r_.reset();
  executor_->child_w_.reset();
  started_ = true;
  result_ = Result();
  return true;
}

Result Sandbox::AwaitResult() {
  if (result_.final_status() != Result::kUnset) return result_;
  if (!started_) {
    result_ = Result::Setup("not started");
    return result_;
  }
  os::Spawned sp;
  sp.process = std::move(process_);
  sp.job = std::move(job_);
  sp.pid = pid_;
  uint64_t wall = executor_->limits()->wall_ms();
  int code = 0;
  result_ = os::Wait(&sp, wall, &code);
  return result_;
}

Result Sandbox::Run() {
  if (!RunAsync()) return result_;
  return AwaitResult();
}

void Sandbox::Kill() {
  os::Spawned sp;
  sp.pid = pid_;
  if (process_.valid()) {
    sp.process = Handle(process_.get());
    os::Kill(&sp);
    sp.process.release();
  } else {
    os::Kill(&sp);
  }
  if (result_.final_status() == Result::kUnset) result_ = Result::Killed();
}

bool Sandbox::IsTerminated() const {
  return result_.final_status() != Result::kUnset;
}

}  // namespace s2
}  // namespace w2g
