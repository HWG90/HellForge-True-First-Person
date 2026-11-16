// Windows stand-ins the retail editor/pose helper needs beyond tests/winshim/windows.h (which is left
// untouched). Test-only. Include after the standard headers and before the retail source.
//  - the structured-exception types its assembly's fault handler names (the handler is never called here);
//  - GetCurrentThreadId and _AddressOfReturnAddress, answered from test variables;
//  - VirtualQuery that also describes unmapped address space as a free region, and VirtualAlloc that honours
//    the requested address: the look-gain normalisation searches for free address space near the game image;
//  - observers for the allocation and a switch that makes it fail.
#pragma once
#include "windows.h"
#include <cerrno>

typedef int EXCEPTION_DISPOSITION;
enum { ExceptionContinueExecution = 0, ExceptionContinueSearch = 1 };
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
struct EXCEPTION_RECORD { DWORD ExceptionCode, ExceptionFlags; EXCEPTION_RECORD *ExceptionRecord; PVOID ExceptionAddress;
                          DWORD NumberParameters; uint64_t ExceptionInformation[15]; };
struct CONTEXT { uint64_t Rip, Rcx; };
enum : DWORD { MEM_FREE = 0x10000 };

inline uint32_t shim_thread_id = 1;                 // what GetCurrentThreadId() answers
inline DWORD GetCurrentThreadId() { return shim_thread_id; }
inline void *shim_address_of_return_address = nullptr;
#define _AddressOfReturnAddress() (shim_address_of_return_address)

// The mapping that holds `at`, or the unmapped gap around it. False above the user address space.
inline bool shim_span(uint64_t at, uint64_t *base, uint64_t *size, bool *is_free) {
  if (at >= 0x800000000000ULL) return false;
  FILE *maps = std::fopen("/proc/self/maps", "r");
  if (!maps) return false;
  char line[512];
  uint64_t gap_begin = 0, gap_end = 0x800000000000ULL;
  bool found = false;
  while (std::fgets(line, sizeof line, maps)) {
    char *cursor = line;
    const uint64_t begin = std::strtoull(cursor, &cursor, 16);
    if (*cursor != '-') continue;
    const uint64_t end = std::strtoull(cursor + 1, &cursor, 16);
    if (at >= begin && at < end) { *base = begin; *size = end - begin; *is_free = false; found = true; break; }
    if (end <= at) gap_begin = end;
    else { if (begin < gap_end) gap_end = begin; break; }
  }
  std::fclose(maps);
  if (!found) { *base = gap_begin; *size = gap_end - gap_begin; *is_free = true; }
  return true;
}
inline SIZE_T pose_VirtualQuery(LPCVOID at, MEMORY_BASIC_INFORMATION *out, SIZE_T size) {
  const SIZE_T mapped = VirtualQuery(at, out, size);
  if (mapped || size < sizeof *out) return mapped;
  uint64_t base = 0, length = 0; bool is_free = false;
  if (!shim_span(reinterpret_cast<uint64_t>(at), &base, &length, &is_free) || !is_free) return 0;
  std::memset(out, 0, sizeof *out);
  out->BaseAddress = reinterpret_cast<void *>(base);
  out->RegionSize = length;
  out->State = MEM_FREE;
  out->Protect = PAGE_NOACCESS;
  return sizeof *out;
}
// Allocation at a requested address (never moved elsewhere, as Windows does not). Observed; can be made to fail.
inline void (*shim_on_allocate)(uint64_t wanted, uint64_t size, uint64_t got) = nullptr;
inline unsigned shim_allocate_failures = 0;         // this many of the next placed allocations fail
inline uint64_t shim_place(uint64_t at, uint64_t size) {
  uint64_t got = 0;
  if (shim_allocate_failures) --shim_allocate_failures;
  else {
    void *p = mmap(reinterpret_cast<void *>(at), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != MAP_FAILED && reinterpret_cast<uint64_t>(p) != at) { munmap(p, size); p = MAP_FAILED; }
    if (p != MAP_FAILED) got = reinterpret_cast<uint64_t>(p);
  }
  if (shim_on_allocate) shim_on_allocate(at, size, got);
  return got;
}
inline LPVOID pose_VirtualAlloc(LPVOID at, SIZE_T size, DWORD type, DWORD protection) {
  if (!at) return VirtualAlloc(at, size, type, protection);
  return reinterpret_cast<LPVOID>(shim_place(reinterpret_cast<uint64_t>(at), size));
}
#define VirtualQuery pose_VirtualQuery
#define VirtualAlloc pose_VirtualAlloc
