#include "os.h"

#include "w2g/s2/comms.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace w2g {
namespace s2 {

void Handle::reset(Native n) {
  if (n_ != Invalid()) CloseHandle(n_);
  n_ = n;
}

namespace os {

void CloseNative(Handle::Native n) {
  if (n) CloseHandle(n);
}

static Handle MakeInheritablePipeEnd(HANDLE h, bool inherit) {
  SetHandleInformation(h, HANDLE_FLAG_INHERIT, inherit ? HANDLE_FLAG_INHERIT : 0);
  return Handle(h);
}

PipeEnds MakeDuplex() {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE p2c_r = nullptr, p2c_w = nullptr;
  HANDLE c2p_r = nullptr, c2p_w = nullptr;
  if (!CreatePipe(&p2c_r, &p2c_w, &sa, 0)) return {};
  if (!CreatePipe(&c2p_r, &c2p_w, &sa, 0)) {
    CloseHandle(p2c_r);
    CloseHandle(p2c_w);
    return {};
  }
  PipeEnds p;
  p.parent_w = MakeInheritablePipeEnd(p2c_w, false);
  p.child_r = MakeInheritablePipeEnd(p2c_r, true);
  p.parent_r = MakeInheritablePipeEnd(c2p_r, false);
  p.child_w = MakeInheritablePipeEnd(c2p_w, true);
  return p;
}

bool Write(Handle::Native n, const void* p, uint32_t len) {
  if (!n || (len > 0 && !p)) return false;
  const uint8_t* b = static_cast<const uint8_t*>(p);
  uint32_t left = len;
  while (left) {
    DWORD nout = 0;
    DWORD chunk = left;
    if (!WriteFile(n, b, chunk, &nout, nullptr) || nout == 0 || nout > left) {
      return false;
    }
    b += nout;
    left -= nout;
  }
  return true;
}

bool Read(Handle::Native n, void* p, uint32_t len) {
  if (!n || (len > 0 && !p)) return false;
  uint8_t* b = static_cast<uint8_t*>(p);
  uint32_t left = len;
  while (left) {
    DWORD nin = 0;
    DWORD chunk = left;
    if (!ReadFile(n, b, chunk, &nin, nullptr) || nin == 0 || nin > left) {
      return false;
    }
    b += nin;
    left -= nin;
  }
  return true;
}

bool CreateMapping(size_t size, Mapping* out) {
  if (!out || size == 0) return false;
  HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                static_cast<DWORD>(static_cast<uint64_t>(size) >> 32),
                                static_cast<DWORD>(size), nullptr);
  if (!h) return false;
  void* p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (!p) {
    CloseHandle(h);
    return false;
  }
  out->handle = Handle(h);
  out->ptr = static_cast<uint8_t*>(p);
  out->size = size;
  return true;
}

bool MapExisting(Handle::Native n, size_t size, Mapping* out) {
  void* p = MapViewOfFile(n, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (!p) return false;
  out->handle = Handle();
  out->ptr = static_cast<uint8_t*>(p);
  out->size = size;
  return true;
}

void Unmap(Mapping* m) {
  if (m->ptr) UnmapViewOfFile(m->ptr);
  m->ptr = nullptr;
}

static std::wstring Widen(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(static_cast<size_t>(n ? n - 1 : 0), L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}

static std::wstring Quote(const std::string& s) {
  std::wstring w = Widen(s);
  bool need = w.empty();
  for (wchar_t c : w) {
    if (c == L' ' || c == L'\t' || c == L'"') {
      need = true;
      break;
    }
  }
  if (!need) return w;
  std::wstring o = L"\"";
  size_t slashes = 0;
  for (wchar_t c : w) {
    if (c == L'\\') {
      ++slashes;
      o += c;
    } else if (c == L'"') {
      o.append(slashes + 1, L'\\');
      o += c;
      slashes = 0;
    } else {
      slashes = 0;
      o += c;
    }
  }
  o.append(slashes, L'\\');
  o += L'"';
  return o;
}

static bool KeepHostEnv(std::wstring_view kv) {
  auto eq = kv.find(L'=');
  if (eq == std::wstring_view::npos || eq == 0) return false;
  auto key = kv.substr(0, eq);
  auto eqi = [](std::wstring_view a, const wchar_t* b) {
    size_t n = 0;
    while (b[n]) ++n;
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
      wchar_t ca = a[i] >= L'a' && a[i] <= L'z' ? a[i] - 32 : a[i];
      wchar_t cb = b[i] >= L'a' && b[i] <= L'z' ? b[i] - 32 : b[i];
      if (ca != cb) return false;
    }
    return true;
  };
  return eqi(key, L"SystemRoot") || eqi(key, L"SYSTEMDRIVE") || eqi(key, L"windir") ||
         eqi(key, L"Path") || eqi(key, L"PATHEXT") ||
         eqi(key, L"NUMBER_OF_PROCESSORS") || eqi(key, L"PROCESSOR_ARCHITECTURE");
}

static std::wstring BuildEnv(const std::vector<std::string>& extra) {
  std::wstring out;
  LPWCH block = GetEnvironmentStringsW();
  if (block) {
    const wchar_t* p = block;
    while (*p) {
      if (KeepHostEnv(p)) {
        out.append(p);
        out.push_back(L'\0');
      }
      p += lstrlenW(p) + 1;
    }
    FreeEnvironmentStringsW(block);
  }
  for (const auto& e : extra) {
    if (e.find('\0') != std::string::npos) continue;
    auto eq = e.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    out += Widen(e);
    out.push_back(L'\0');
  }
  out.push_back(L'\0');
  return out;
}

static bool ApplyJobLimits(HANDLE job, const Limits* lim, const Policy* pol) {
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION ex{};
  ex.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (pol && !pol->allow_spawn()) {
    ex.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    ex.BasicLimitInformation.ActiveProcessLimit = 1;
  }
  if (lim && lim->memory_bytes()) {
    ex.BasicLimitInformation.LimitFlags |=
        JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_MEMORY;
    ex.ProcessMemoryLimit = static_cast<SIZE_T>(lim->memory_bytes());
    ex.JobMemoryLimit = static_cast<SIZE_T>(lim->memory_bytes());
  }
  if (lim && lim->cpu_seconds()) {
    ex.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME;
    ex.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart =
        static_cast<LONGLONG>(lim->cpu_seconds()) * 10000000LL;
  }
  if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &ex,
                              sizeof(ex))) {
    return true;
  }
  ex.BasicLimitInformation.LimitFlags &= ~JOB_OBJECT_LIMIT_JOB_MEMORY;
  ex.JobMemoryLimit = 0;
  if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &ex,
                              sizeof(ex))) {
    return true;
  }
  ex.BasicLimitInformation.LimitFlags &= ~JOB_OBJECT_LIMIT_PROCESS_MEMORY;
  ex.ProcessMemoryLimit = 0;
  return SetInformationJobObject(job, JobObjectExtendedLimitInformation, &ex,
                                 sizeof(ex)) != 0;
}

Result Spawn(const SpawnReq& req, Spawned* out) {
  if (!out) return Result::Setup("nil spawn out");
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job) return Result::Setup("CreateJobObject");
  if (!ApplyJobLimits(job, req.limits, req.policy)) {
    CloseHandle(job);
    return Result::Setup("SetInformationJobObject");
  }

  std::wstring cmd;
  if (!req.argv.empty()) {
    for (size_t i = 0; i < req.argv.size(); ++i) {
      if (i) cmd += L' ';
      cmd += Quote(req.argv[i]);
    }
  } else {
    cmd = Quote(req.path);
  }

  std::vector<std::string> extra;
  for (const auto& e : req.env) {
    if (e.find('\0') != std::string::npos) continue;
    auto eq = e.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    if (e.compare(0, eq, kCommsEnv) == 0) continue;
    extra.push_back(e);
  }
  if (req.child_r != Handle::Invalid() && req.child_w != Handle::Invalid()) {
    extra.push_back(std::string(kCommsEnv) + "=" +
                    CommsEnvValue(req.child_r, req.child_w));
  }
  std::wstring env = BuildEnv(extra);

  STARTUPINFOEXW siex{};
  siex.StartupInfo.cb = sizeof(siex);
  SIZE_T asz = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &asz);
  std::vector<unsigned char> abuf(asz);
  auto* al = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(abuf.data());
  if (!InitializeProcThreadAttributeList(al, 1, 0, &asz)) {
    CloseHandle(job);
    return Result::Setup("InitializeProcThreadAttributeList");
  }
  HANDLE inherit[2] = {req.child_r, req.child_w};
  if (req.child_r != Handle::Invalid() && req.child_w != Handle::Invalid()) {
    if (!UpdateProcThreadAttribute(al, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherit, sizeof(inherit), nullptr, nullptr)) {
      DeleteProcThreadAttributeList(al);
      CloseHandle(job);
      return Result::Setup("HANDLE_LIST");
    }
  }
  siex.lpAttributeList = al;

  PROCESS_INFORMATION pi{};
  DWORD flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                CREATE_BREAKAWAY_FROM_JOB | EXTENDED_STARTUPINFO_PRESENT;
  std::wstring path = Widen(req.path);
  std::wstring cwd = Widen(req.cwd);
  std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
  cmdline.push_back(0);

  BOOL inherit_handles =
      (req.child_r != Handle::Invalid() && req.child_w != Handle::Invalid())
          ? TRUE
          : FALSE;
  BOOL ok = CreateProcessW(path.empty() ? nullptr : path.c_str(), cmdline.data(),
                           nullptr, nullptr, inherit_handles, flags, env.data(),
                           cwd.empty() ? nullptr : cwd.c_str(),
                           &siex.StartupInfo, &pi);
  DeleteProcThreadAttributeList(al);
  if (!ok) {
    CloseHandle(job);
    return Result::Setup("CreateProcess");
  }
  if (!AssignProcessToJobObject(job, pi.hProcess)) {
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(job);
    return Result::Setup("AssignProcessToJobObject");
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  out->process = Handle(pi.hProcess);
  out->job = Handle(job);
  out->pid = pi.dwProcessId;
  return Result::Ok();
}

Result Wait(Spawned* p, uint64_t timeout_ms, int* exit_code) {
  if (!p || !p->process.valid()) return Result::Internal("no process");
  DWORD t = (timeout_ms == 0 || timeout_ms > 0xffffffffULL)
                ? INFINITE
                : static_cast<DWORD>(timeout_ms);
  DWORD w = WaitForSingleObject(p->process.get(), t);
  if (w == WAIT_TIMEOUT) return Result::Timeout();
  if (w != WAIT_OBJECT_0) return Result::Internal("WaitForSingleObject");
  DWORD code = 0;
  GetExitCodeProcess(p->process.get(), &code);
  if (exit_code) *exit_code = static_cast<int>(code);
  return Result::Ok(static_cast<int>(code));
}

void Kill(Spawned* p) {
  if (p && p->process.valid()) TerminateProcess(p->process.get(), 1);
}

bool CommsFromEnv(Handle* r, Handle* w) {
  const char* e = std::getenv(kCommsEnv);
  if (!e || !*e || !r || !w) return false;
  char* end = nullptr;
  unsigned long long rv = std::strtoull(e, &end, 10);
  if (!end || *end != ',') return false;
  unsigned long long wv = std::strtoull(end + 1, nullptr, 10);
  *r = Handle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(rv)));
  *w = Handle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(wv)));
  return r->valid() && w->valid();
}

std::string CommsEnvValue(Handle::Native r, Handle::Native w) {
  std::ostringstream o;
  o << reinterpret_cast<uintptr_t>(r) << ',' << reinterpret_cast<uintptr_t>(w);
  return o.str();
}

}  // namespace os
}  // namespace s2
}  // namespace w2g
