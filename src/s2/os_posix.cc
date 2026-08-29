#include "os.h"

#include "w2g/s2/comms.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace w2g {
namespace s2 {

void Handle::reset(Native n) {
  if (n_ != Invalid()) ::close(n_);
  n_ = n;
}

namespace os {

void CloseNative(Handle::Native n) {
  if (n >= 0) ::close(n);
}

PipeEnds MakeDuplex() {
  int a[2] = {-1, -1};
  int b[2] = {-1, -1};
  if (pipe(a) != 0) return {};
  if (pipe(b) != 0) {
    ::close(a[0]);
    ::close(a[1]);
    return {};
  }
  PipeEnds p;
  fcntl(a[1], F_SETFD, FD_CLOEXEC);
  fcntl(b[0], F_SETFD, FD_CLOEXEC);
  p.parent_w = Handle(a[1]);
  p.child_r = Handle(a[0]);
  p.parent_r = Handle(b[0]);
  p.child_w = Handle(b[1]);
  return p;
}

bool Write(Handle::Native n, const void* p, uint32_t len) {
  if (n < 0 || (len > 0 && !p)) return false;
  const uint8_t* b = static_cast<const uint8_t*>(p);
  uint32_t left = len;
  while (left) {
    ssize_t nout = ::write(n, b, left);
    if (nout < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (nout == 0) return false;
    b += nout;
    left -= static_cast<uint32_t>(nout);
  }
  return true;
}

bool Read(Handle::Native n, void* p, uint32_t len) {
  if (n < 0 || (len > 0 && !p)) return false;
  uint8_t* b = static_cast<uint8_t*>(p);
  uint32_t left = len;
  while (left) {
    ssize_t nin = ::read(n, b, left);
    if (nin < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (nin == 0) return false;
    b += nin;
    left -= static_cast<uint32_t>(nin);
  }
  return true;
}

bool CreateMapping(size_t size, Mapping* out) {
  if (!out || size == 0) return false;
  void* p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) return false;
  out->handle = Handle();
  out->ptr = static_cast<uint8_t*>(p);
  out->size = size;
  return true;
}

bool MapExisting(Handle::Native n, size_t size, Mapping* out) {
  void* p = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, n, 0);
  if (p == MAP_FAILED) return false;
  out->handle = Handle();
  out->ptr = static_cast<uint8_t*>(p);
  out->size = size;
  return true;
}

void Unmap(Mapping* m) {
  if (m->ptr && m->size) ::munmap(m->ptr, m->size);
  m->ptr = nullptr;
}

static void ApplyLimits(const Limits* lim) {
  if (!lim) return;
  auto set = [](int res, uint64_t n) {
    if (!n) return;
    rlimit rl{};
    rl.rlim_cur = n;
    rl.rlim_max = n;
    setrlimit(res, &rl);
  };
  if (lim->memory_bytes()) set(RLIMIT_AS, lim->memory_bytes());
  if (lim->cpu_seconds()) set(RLIMIT_CPU, lim->cpu_seconds());
  if (lim->file_bytes()) set(RLIMIT_FSIZE, lim->file_bytes());
  if (lim->max_files()) set(RLIMIT_NOFILE, lim->max_files());
}

Result Spawn(const SpawnReq& req, Spawned* out) {
  if (!out) return Result::Setup("nil spawn out");
  pid_t pid = ::fork();
  if (pid < 0) return Result::Setup("fork");
  if (pid == 0) {
    ApplyLimits(req.limits);
    if (!req.cwd.empty() && ::chdir(req.cwd.c_str()) != 0) _exit(127);
    int cr = req.child_r;
    int cw = req.child_w;
    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 1024) maxfd = 256;
    for (int fd = 3; fd < maxfd; ++fd) {
      if (fd != cr && fd != cw) ::close(fd);
    }
    std::vector<std::string> envstore;
    envstore.push_back(std::string(kCommsEnv) + "=" + CommsEnvValue(cr, cw));
    for (const auto& e : req.env) {
      if (e.find('\0') != std::string::npos) continue;
      auto eq = e.find('=');
      if (eq == std::string::npos || eq == 0) continue;
      if (e.compare(0, eq, kCommsEnv) == 0) continue;
      envstore.push_back(e);
    }
    std::vector<char*> envp;
    for (auto& e : envstore) envp.push_back(e.data());
    envp.push_back(nullptr);
    std::vector<char*> argv;
    for (auto& a : const_cast<std::vector<std::string>&>(req.argv)) {
      argv.push_back(a.data());
    }
    if (argv.empty()) {
      argv.push_back(const_cast<char*>(req.path.c_str()));
    }
    argv.push_back(nullptr);
    execve(req.path.c_str(), argv.data(), envp.data());
    _exit(127);
  }
  out->pid = static_cast<uint64_t>(pid);
  return Result::Ok();
}

Result Wait(Spawned* p, uint64_t timeout_ms, int* exit_code) {
  if (!p) return Result::Internal("no process");
  int st = 0;
  pid_t pid = static_cast<pid_t>(p->pid);
  if (timeout_ms == 0) {
    if (waitpid(pid, &st, 0) < 0) return Result::Internal("waitpid");
  } else {
    // Poll. Clean-room: no signalfd.
    uint64_t waited = 0;
    for (;;) {
      pid_t r = waitpid(pid, &st, WNOHANG);
      if (r == pid) break;
      if (r < 0) return Result::Internal("waitpid");
      if (waited >= timeout_ms) return Result::Timeout();
      usleep(10000);
      waited += 10;
    }
  }
  if (WIFSIGNALED(st)) return Result::Signaled(WTERMSIG(st));
  int code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
  if (exit_code) *exit_code = code;
  return Result::Ok(code);
}

void Kill(Spawned* p) {
  if (p && p->pid) ::kill(static_cast<pid_t>(p->pid), SIGKILL);
}

bool CommsFromEnv(Handle* r, Handle* w) {
  const char* e = std::getenv(kCommsEnv);
  if (!e || !*e || !r || !w) return false;
  char* end = nullptr;
  long rv = std::strtol(e, &end, 10);
  if (!end || *end != ',') return false;
  long wv = std::strtol(end + 1, nullptr, 10);
  *r = Handle(static_cast<int>(rv));
  *w = Handle(static_cast<int>(wv));
  return r->valid() && w->valid();
}

std::string CommsEnvValue(Handle::Native r, Handle::Native w) {
  std::ostringstream o;
  o << r << ',' << w;
  return o.str();
}

}  // namespace os
}  // namespace s2
}  // namespace w2g
