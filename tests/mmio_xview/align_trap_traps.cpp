#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// tests/mmio_xview/align_trap_traps.cpp
//
// Contract (§11):
//   If the alignment policy is align_policy::trap, the library checks alignment at runtime
//   and calls a trap instruction on failure (e.g. __builtin_trap / __fastfail).
//   This is unconditional fail-fast and does not depend on MAD_ASSERT.
//
// This file is a *runtime death-test*.
// To keep it self-contained and cross-platform-ish, we implement the trap validation as:
//   - On POSIX (Linux/macOS): fork() a child, trigger the misaligned make_xview in child,
//     then parent waits and asserts the child terminated due to a signal.
//   - On non-POSIX platforms: we still compile and we validate the aligned case, but we
//     cannot portably assert "died by trap" without platform-specific process APIs.
//     In that case, the file reports "skipped" by returning 0 after aligned checks.
//
// The contract requirement is strongest on POSIX where we can observe the trap reliably.
//
// IMPORTANT:
#include "madpacket.hpp"

#if defined(__unix__) || defined(__APPLE__)
  #include <sys/wait.h>
  #include <unistd.h>
#endif

static inline bool is_posix() {
#if defined(__unix__) || defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

int main() {
  using Bus = mad::reg::bus32;
  static_assert(Bus::align == 4);

  using P = mad::packet<
    mad::u32<"w0">,
    mad::u8<"tail">
  >;
  static_assert(P::total_bytes == 5);

  using CfgTrap8 = mad::reg::cfg<Bus, 8, mad::reg::width_policy::native, mad::reg::align_policy::trap>;

  alignas(8) std::array<std::byte, P::total_bytes + 8> storage{};
  volatile std::byte* base_aligned = reinterpret_cast<volatile std::byte*>(storage.data());
  volatile std::byte* base_misaligned = base_aligned + 1;

  // Aligned construction must succeed and not trap.
  auto ok = mad::reg::make_xview<P, CfgTrap8>(reinterpret_cast<volatile void*>(base_aligned));
  ok.set<"tail">(0x5Au);
  assert(ok.get<"tail">() == 0x5Au);

  // Death-test: misaligned pointer should trap.
  if (!is_posix()) {
    // Not portable to validate trap without a harness. We at least ensure compilation and
    // aligned behavior above. A platform harness can implement process-level death tests.
    return 0;
  }

#if defined(__unix__) || defined(__APPLE__)
  pid_t pid = fork();
  if (pid == 0) {
    // Child: trigger trap. If it doesn't trap, exit with a distinctive code.
    (void)mad::reg::make_xview<P, CfgTrap8>(reinterpret_cast<volatile void*>(base_misaligned));
    _exit(42);
  }

  int status = 0;
  pid_t w = waitpid(pid, &status, 0);
  assert(w == pid);

  // We expect the child to have been terminated by a signal.
  // SIGILL or SIGTRAP are typical for __builtin_trap, but exact signal is platform/compiler dependent.
  assert(WIFSIGNALED(status) && "child should be terminated by signal due to trap");

  const int sig = WTERMSIG(status);

  // Be permissive: accept common trap-related signals.
  // This still catches the important invariant: it did not exit normally.
  const bool ok_sig =
      (sig == SIGILL)  ||
      (sig == SIGTRAP) ||
      (sig == SIGABRT) ||
      (sig == SIGSEGV);
  assert(ok_sig && "unexpected signal for trap; still should be signal-terminated");

  // Additionally, ensure it did NOT exit with our sentinel code.
  assert(!(WIFEXITED(status) && WEXITSTATUS(status) == 42));

  return 0;
#else
  return 0;
#endif
}
