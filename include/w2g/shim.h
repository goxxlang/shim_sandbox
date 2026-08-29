#ifndef W2G_SHIM_H_
#define W2G_SHIM_H_

// ABAC gate in front of WASIGo++'s host OS shim (runtime.hpp os_open /
// os_create / os_read_file / os_write_file / os_getenv / os_exit). The
// generated WASI layer calls these instead of ambient fopen.
//
// System sandbox access is a compile-time opt-in: this TU must be built
// with -DW2G_ABAC_SYSTEM=1. Without that, every host call returns
// kSystemDisabled and never calls fopen/getenv. Runtime ABAC is still
// default-deny when the compile-time gate is on.

#include "w2g/abac.h"
#include "w2g/system_policy.h"

#include "runtime.hpp"

#include <string>

namespace w2g {

class Shim {
 public:
  Shim() { ApplyCompilePolicy(); }
  explicit Shim(abac::Engine engine, std::string subject = "wasi")
      : engine_(std::move(engine)), subject_(std::move(subject)) {
    ApplyCompilePolicy();
  }

  void set_subject(std::string s) { subject_ = std::move(s); }
  const std::string& subject() const { return subject_; }
  abac::Engine* engine() { return &engine_; }
  const abac::Engine* engine() const { return &engine_; }

  bool Check(std::string_view action, std::string_view resource) const {
    return engine_.Check(subject_, action, resource);
  }

  wasigo::OsOpenResult Open(const std::string& name);
  wasigo::OsOpenResult Create(const std::string& name);
  wasigo::OsReadFileResult ReadFile(const std::string& name);
  wasigo::Error WriteFile(const std::string& name, wasigo::Slice<uint8_t> data,
                          int64_t perm);
  std::string Getenv(const std::string& key);
  void Exit(int64_t code);

  static bool SystemEnabled() { return kSystemSandboxEnabled; }

 private:
  void ApplyCompilePolicy() { W2G_ABAC_APPLY(engine_); }
  wasigo::Error Denied(std::string_view action, const std::string& resource) const;
  wasigo::Error CompileDisabled() const {
    return wasigo::errors_new(kSystemDisabled);
  }

  wasigo::OsOpenResult OpenHost(const std::string& name);
  wasigo::OsOpenResult CreateHost(const std::string& name);
  wasigo::OsReadFileResult ReadFileHost(const std::string& name);
  wasigo::Error WriteFileHost(const std::string& name, wasigo::Slice<uint8_t> data,
                              int64_t perm);
  std::string GetenvHost(const std::string& key);

  abac::Engine engine_;
  std::string subject_ = "wasi";
};

inline wasigo::OsOpenResult Shim::Open(const std::string& name) {
#if W2G_ABAC_SYSTEM
  if (!Check(abac::kOpen, name)) return {wasigo::File{}, Denied(abac::kOpen, name)};
  return OpenHost(name);
#else
  (void)name;
  return {wasigo::File{}, CompileDisabled()};
#endif
}

inline wasigo::OsOpenResult Shim::Create(const std::string& name) {
#if W2G_ABAC_SYSTEM
  if (!Check(abac::kCreate, name)) {
    return {wasigo::File{}, Denied(abac::kCreate, name)};
  }
  return CreateHost(name);
#else
  (void)name;
  return {wasigo::File{}, CompileDisabled()};
#endif
}

inline wasigo::OsReadFileResult Shim::ReadFile(const std::string& name) {
#if W2G_ABAC_SYSTEM
  if (!Check(abac::kReadFile, name)) {
    return {wasigo::Slice<uint8_t>{}, Denied(abac::kReadFile, name)};
  }
  return ReadFileHost(name);
#else
  (void)name;
  return {wasigo::Slice<uint8_t>{}, CompileDisabled()};
#endif
}

inline wasigo::Error Shim::WriteFile(const std::string& name,
                                     wasigo::Slice<uint8_t> data, int64_t perm) {
#if W2G_ABAC_SYSTEM
  if (!Check(abac::kWriteFile, name)) return Denied(abac::kWriteFile, name);
  return WriteFileHost(name, data, perm);
#else
  (void)name;
  (void)data;
  (void)perm;
  return CompileDisabled();
#endif
}

inline std::string Shim::Getenv(const std::string& key) {
#if W2G_ABAC_SYSTEM
  if (key.find('\0') != std::string::npos) return {};
  if (!Check(abac::kGetenv, key)) return {};
  return GetenvHost(key);
#else
  (void)key;
  return {};
#endif
}

inline void Shim::Exit(int64_t) {
#if W2G_ABAC_SYSTEM
  (void)Check(abac::kExit, "*");
#endif
}

}  // namespace w2g

#endif  // W2G_SHIM_H_
