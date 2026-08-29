#ifndef W2G_SYSTEM_POLICY_H_
#define W2G_SYSTEM_POLICY_H_

// Compile-time System sandbox gate.
//
// Host OS access (files, env, s2 spawn from a consumer TU's Shim) is
// denied at compile time unless this TU is built with -DW2G_ABAC_SYSTEM=1
// AND an ABAC policy allows the action at runtime (default deny).
//
// Operator policy (optional):
//   -DW2G_ABAC_POLICY_HEADER="my_policy.h"
// my_policy.h defines W2G_ABAC_APPLY(engine) to Allow/Deny rules.

#ifndef W2G_ABAC_SYSTEM
#define W2G_ABAC_SYSTEM 0
#endif

#if W2G_ABAC_SYSTEM
#ifdef W2G_ABAC_POLICY_HEADER
#include W2G_ABAC_POLICY_HEADER
#endif
#ifndef W2G_ABAC_APPLY
#define W2G_ABAC_APPLY(engine) ((void)0)
#endif
#else
#ifdef W2G_ABAC_APPLY
#undef W2G_ABAC_APPLY
#endif
#define W2G_ABAC_APPLY(engine) ((void)0)
#endif

namespace w2g {

inline constexpr bool kSystemSandboxEnabled = (W2G_ABAC_SYSTEM != 0);

inline const char* kSystemDisabled =
    "system sandbox disabled at compile time (need -DW2G_ABAC_SYSTEM=1 and an ABAC policy)";

}  // namespace w2g

#endif  // W2G_SYSTEM_POLICY_H_
