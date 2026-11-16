// A stand-in for the few Windows calls the retail first-person helpers use,
// so their sources can be compiled natively as a reference for differential
// tests. Test-only. Memory calls act on this process's real pages, as the
// originals do on Windows; faults are reported, not raised.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include <chrono>

typedef uint32_t DWORD;
typedef int BOOL;
typedef size_t SIZE_T;
typedef void *HANDLE;
typedef void *HMODULE;
typedef void *LPVOID;
typedef void *PVOID;
typedef const void *LPCVOID;
typedef const wchar_t *LPCWSTR;
typedef uint64_t DWORD64;
typedef union { struct { uint32_t LowPart; int32_t HighPart; }; int64_t QuadPart; } LARGE_INTEGER;
typedef struct { void *BaseAddress, *AllocationBase; DWORD AllocationProtect; uint16_t PartitionId; SIZE_T RegionSize;
                 DWORD State, Protect, Type; } MEMORY_BASIC_INFORMATION;
typedef struct { pthread_rwlock_t lock; } SRWLOCK;
#define SRWLOCK_INIT {PTHREAD_RWLOCK_INITIALIZER}
#define WINAPI
#define __declspec(x)
#define __cdecl
// Structured exception handling: the guarded statement runs; the handler never does.
// libstdc++ defines __try for its own use; a file that includes this shim must include its
// standard headers first (see tests/retail_camera_ref.h).
#undef __try
#define __try if (true)
#define __except(filter) else
#define EXCEPTION_EXECUTE_HANDLER 1
#define INFINITE 0xffffffffu
enum : DWORD { PAGE_NOACCESS = 1, PAGE_READONLY = 2, PAGE_READWRITE = 4, PAGE_WRITECOPY = 8, PAGE_EXECUTE = 0x10,
               PAGE_EXECUTE_READ = 0x20, PAGE_EXECUTE_READWRITE = 0x40, PAGE_EXECUTE_WRITECOPY = 0x80, PAGE_GUARD = 0x100 };
enum : DWORD { MEM_RELEASE = 0x8000, WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 258 };
#define TRUE 1
#define FALSE 0
enum : DWORD { MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, MEM_PRIVATE = 0x20000, MEM_MAPPED = 0x40000, MEM_IMAGE = 0x1000000 };
enum : DWORD { GET_MODULE_HANDLE_EX_FLAG_PIN = 1, GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS = 4 };
#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE 0x00004550
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20b
#pragma pack(push, 1)
typedef struct { uint16_t e_magic; uint8_t pad[58]; int32_t e_lfanew; } IMAGE_DOS_HEADER;
typedef struct { uint16_t Machine, NumberOfSections; uint32_t TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
                 uint16_t SizeOfOptionalHeader, Characteristics; } IMAGE_FILE_HEADER;
typedef struct { uint16_t Magic; uint8_t pad[54]; uint32_t SizeOfImage; uint8_t rest[180]; } IMAGE_OPTIONAL_HEADER64;
typedef struct { uint32_t Signature; IMAGE_FILE_HEADER FileHeader; IMAGE_OPTIONAL_HEADER64 OptionalHeader; } IMAGE_NT_HEADERS64;
#pragma pack(pop)

// ---- test controls -----------------------------------------------------------------------
// Regions the test declares as images, so VirtualQuery can answer MEM_IMAGE with their base.
struct ShimRegion { uint64_t base, size; DWORD type, protect; };
inline ShimRegion shim_regions[16];
inline unsigned shim_region_count = 0;
inline void *shim_return_address = nullptr;   // what _ReturnAddress() answers inside the helper's hook
inline double shim_clock_ms = 1000;           // QueryPerformanceCounter / GetTickCount64
inline void shim_region(uint64_t base, uint64_t size, DWORD type, DWORD protect) {
  shim_regions[shim_region_count++] = ShimRegion{base, size, type, protect};
}

// Observers for a differential test (all optional): every read (before and after), write, compare-and-swap
// and lock operation the helper makes, in order; and a switch that makes module pinning fail.
inline void (*shim_on_read)(uint64_t at, size_t size, int after, bool complete) = nullptr;
inline void (*shim_on_write)(uint64_t at, size_t size, bool complete) = nullptr;
inline void (*shim_on_cas)(uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) = nullptr;
inline void (*shim_on_lock)(void *lock, int operation) = nullptr; // 0/1 exclusive acquire/release, 2/3 shared
inline bool shim_pin_fails = false;

// This process's id, asked once (a test that forks after its first read sets shim_self back to 0 in the child).
inline pid_t shim_self = 0;
inline pid_t shim_pid() { if (!shim_self) shim_self = getpid(); return shim_self; }
inline HANDLE GetCurrentProcess() { return nullptr; }
inline BOOL ReadProcessMemory(HANDLE, LPCVOID at, LPVOID out, SIZE_T size, SIZE_T *done) {
  if (shim_on_read) shim_on_read(reinterpret_cast<uint64_t>(at), size, 0, false);
  struct iovec local = {out, size}, remote = {const_cast<void *>(at), size};
  ssize_t n = process_vm_readv(shim_pid(), &local, 1, &remote, 1, 0);
  if (done) *done = n < 0 ? 0 : static_cast<SIZE_T>(n);
  if (shim_on_read) shim_on_read(reinterpret_cast<uint64_t>(at), size, 1, n == static_cast<ssize_t>(size));
  return n == static_cast<ssize_t>(size);
}
inline BOOL WriteProcessMemory(HANDLE, LPVOID at, LPCVOID in, SIZE_T size, SIZE_T *done) {
  struct iovec local = {const_cast<void *>(in), size}, remote = {at, size};
  ssize_t n = process_vm_writev(shim_pid(), &local, 1, &remote, 1, 0);
  if (done) *done = n < 0 ? 0 : static_cast<SIZE_T>(n);
  if (shim_on_write) shim_on_write(reinterpret_cast<uint64_t>(at), size, n == static_cast<ssize_t>(size));
  return n == static_cast<ssize_t>(size);
}
// Pages: answered from /proc/self/maps, so protection changes and the test binary's own code are seen as they are.
inline uint64_t shim_allocations[256][2];
inline unsigned shim_allocation_count = 0;
inline DWORD shim_protection(const char *perms) {
  const bool r = perms[0] == 'r', w = perms[1] == 'w', x = perms[2] == 'x';
  if (!r && !w && !x) return PAGE_NOACCESS;
  if (x) return w ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
  return w ? PAGE_READWRITE : PAGE_READONLY;
}
inline SIZE_T VirtualQuery(LPCVOID at, MEMORY_BASIC_INFORMATION *out, SIZE_T size) {
  if (size < sizeof *out) return 0;
  const uint64_t a = reinterpret_cast<uint64_t>(at);
  for (unsigned i = 0; i < shim_region_count; ++i) {
    const ShimRegion &r = shim_regions[i];
    if (a >= r.base && a - r.base < r.size) {
      std::memset(out, 0, sizeof *out);
      out->BaseAddress = out->AllocationBase = reinterpret_cast<void *>(r.base);
      out->RegionSize = r.size;
      out->State = MEM_COMMIT;
      out->Protect = out->AllocationProtect = r.protect;
      out->Type = r.type;
      return sizeof *out;
    }
  }
  FILE *maps = std::fopen("/proc/self/maps", "r");
  if (!maps) return 0;
  char line[512];
  SIZE_T result = 0;
  while (std::fgets(line, sizeof line, maps)) {
    unsigned long long begin = 0, end = 0, offset = 0, inode = 0;
    char perms[8] = "", device[16] = "";
    char *cursor = line;
    begin = std::strtoull(cursor, &cursor, 16);
    if (*cursor != '-') continue;
    end = std::strtoull(cursor + 1, &cursor, 16);
    if (a < begin || a >= end) continue; // most lines: no need to parse the rest
    if (std::sscanf(line, "%llx-%llx %7s %llx %15s %llu", &begin, &end, perms, &offset, device, &inode) < 6) continue;
    std::memset(out, 0, sizeof *out);
    out->BaseAddress = reinterpret_cast<void *>(begin);
    out->AllocationBase = out->BaseAddress;
    for (unsigned i = 0; i < shim_allocation_count; ++i)
      if (a >= shim_allocations[i][0] && a - shim_allocations[i][0] < shim_allocations[i][1])
        out->AllocationBase = reinterpret_cast<void *>(shim_allocations[i][0]);
    out->RegionSize = end - begin;
    out->State = MEM_COMMIT;
    out->Protect = out->AllocationProtect = shim_protection(perms);
    out->Type = inode ? MEM_IMAGE : MEM_PRIVATE;
    result = sizeof *out;
    break;
  }
  std::fclose(maps);
  return result;
}
inline int shim_native_protection(DWORD protection) {
  switch (protection & 0xff) {
  case PAGE_NOACCESS: return PROT_NONE;
  case PAGE_READONLY: return PROT_READ;
  case PAGE_READWRITE: case PAGE_WRITECOPY: return PROT_READ | PROT_WRITE;
  case PAGE_EXECUTE: case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
  default: return PROT_READ | PROT_WRITE | PROT_EXEC;
  }
}
inline LPVOID VirtualAlloc(LPVOID, SIZE_T size, DWORD, DWORD protection) {
  const SIZE_T rounded = (size + 4095) & ~static_cast<SIZE_T>(4095);
  void *p = mmap(nullptr, rounded, shim_native_protection(protection), MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) return nullptr;
  if (shim_allocation_count < 256) {
    shim_allocations[shim_allocation_count][0] = reinterpret_cast<uint64_t>(p);
    shim_allocations[shim_allocation_count++][1] = rounded;
  }
  return p;
}
inline BOOL VirtualFree(LPVOID at, SIZE_T, DWORD) {
  for (unsigned i = 0; i < shim_allocation_count; ++i)
    if (shim_allocations[i][0] == reinterpret_cast<uint64_t>(at)) {
      munmap(at, shim_allocations[i][1]);
      shim_allocations[i][0] = shim_allocations[i][1] = 0;
      return 1;
    }
  return 0;
}
inline BOOL VirtualProtect(LPVOID at, SIZE_T size, DWORD protection, DWORD *old) {
  MEMORY_BASIC_INFORMATION before{};
  if (!VirtualQuery(at, &before, sizeof before)) return 0;
  const uint64_t begin = reinterpret_cast<uint64_t>(at) & ~4095ull;
  const uint64_t end = (reinterpret_cast<uint64_t>(at) + size + 4095) & ~4095ull;
  if (mprotect(reinterpret_cast<void *>(begin), end - begin, shim_native_protection(protection))) return 0;
  if (old) *old = before.Protect;
  return 1;
}
inline BOOL FlushInstructionCache(HANDLE, LPCVOID, SIZE_T) { return 1; }
inline void Sleep(DWORD milliseconds) { usleep(milliseconds * 1000); }
// Events.
struct ShimEvent { std::mutex mutex; std::condition_variable changed; bool set = false, manual = false; };
inline HANDLE CreateEventW(void *, BOOL manual, BOOL initial, LPCWSTR) {
  ShimEvent *e = new ShimEvent;
  e->manual = manual != 0;
  e->set = initial != 0;
  return e;
}
inline BOOL SetEvent(HANDLE h) {
  ShimEvent *e = static_cast<ShimEvent *>(h);
  { std::lock_guard<std::mutex> guard(e->mutex); e->set = true; }
  e->changed.notify_all();
  return 1;
}
inline DWORD WaitForSingleObject(HANDLE h, DWORD milliseconds) {
  ShimEvent *e = static_cast<ShimEvent *>(h);
  std::unique_lock<std::mutex> guard(e->mutex);
  if (milliseconds == INFINITE) e->changed.wait(guard, [e] { return e->set; });
  else if (!e->changed.wait_for(guard, std::chrono::milliseconds(milliseconds), [e] { return e->set; })) return WAIT_TIMEOUT;
  if (!e->manual) e->set = false;
  return WAIT_OBJECT_0;
}
inline BOOL CloseHandle(HANDLE h) { delete static_cast<ShimEvent *>(h); return 1; }
inline HMODULE GetModuleHandleW(LPCWSTR) { return nullptr; }
inline void *GetProcAddress(HMODULE, const char *) { return nullptr; }
inline BOOL GetModuleHandleExW(DWORD, LPCWSTR, HMODULE *out) {
  if (shim_pin_fails) return 0;
  if (out) *out = reinterpret_cast<HMODULE>(1);
  return 1;
}
inline void AcquireSRWLockExclusive(SRWLOCK *l) { if (shim_on_lock) shim_on_lock(l, 0); pthread_rwlock_wrlock(&l->lock); }
inline void ReleaseSRWLockExclusive(SRWLOCK *l) { if (shim_on_lock) shim_on_lock(l, 1); pthread_rwlock_unlock(&l->lock); }
inline void AcquireSRWLockShared(SRWLOCK *l) { if (shim_on_lock) shim_on_lock(l, 2); pthread_rwlock_rdlock(&l->lock); }
inline void ReleaseSRWLockShared(SRWLOCK *l) { if (shim_on_lock) shim_on_lock(l, 3); pthread_rwlock_unlock(&l->lock); }
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000; return 1; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER *c) { c->QuadPart = static_cast<int64_t>(shim_clock_ms * 1000); return 1; }
inline uint64_t GetTickCount64() { return static_cast<uint64_t>(shim_clock_ms); }
inline void *InterlockedCompareExchangePointer(void *volatile *target, void *exchange, void *comparand) {
  void *observed = __sync_val_compare_and_swap(target, comparand, exchange);
  if (shim_on_cas) shim_on_cas(reinterpret_cast<uint64_t>(target), reinterpret_cast<uint64_t>(exchange),
                               reinterpret_cast<uint64_t>(comparand), reinterpret_cast<uint64_t>(observed));
  return observed;
}
inline void *InterlockedExchangePointer(void *volatile *target, void *value) {
  return __sync_lock_test_and_set(target, value);
}
