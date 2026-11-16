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
typedef struct { int unused; } SRWLOCK;
#define SRWLOCK_INIT {0}
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

inline HANDLE GetCurrentProcess() { return nullptr; }
inline BOOL ReadProcessMemory(HANDLE, LPCVOID at, LPVOID out, SIZE_T size, SIZE_T *done) {
  struct iovec local = {out, size}, remote = {const_cast<void *>(at), size};
  ssize_t n = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
  if (done) *done = n < 0 ? 0 : static_cast<SIZE_T>(n);
  return n == static_cast<ssize_t>(size);
}
inline BOOL WriteProcessMemory(HANDLE, LPVOID at, LPCVOID in, SIZE_T size, SIZE_T *done) {
  struct iovec local = {const_cast<void *>(in), size}, remote = {at, size};
  ssize_t n = process_vm_writev(getpid(), &local, 1, &remote, 1, 0);
  if (done) *done = n < 0 ? 0 : static_cast<SIZE_T>(n);
  return n == static_cast<ssize_t>(size);
}
inline SIZE_T VirtualQuery(LPCVOID at, MEMORY_BASIC_INFORMATION *out, SIZE_T size) {
  if (size < sizeof *out) return 0;
  uint64_t a = reinterpret_cast<uint64_t>(at);
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
  return 0;
}
inline BOOL VirtualProtect(LPVOID, SIZE_T, DWORD, DWORD *old) { if (old) *old = PAGE_READWRITE; return 1; }
inline HMODULE GetModuleHandleW(LPCWSTR) { return nullptr; }
inline void *GetProcAddress(HMODULE, const char *) { return nullptr; }
inline BOOL GetModuleHandleExW(DWORD, LPCWSTR, HMODULE *out) { if (out) *out = reinterpret_cast<HMODULE>(1); return 1; }
inline void AcquireSRWLockExclusive(SRWLOCK *) {}
inline void ReleaseSRWLockExclusive(SRWLOCK *) {}
inline void AcquireSRWLockShared(SRWLOCK *) {}
inline void ReleaseSRWLockShared(SRWLOCK *) {}
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000; return 1; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER *c) { c->QuadPart = static_cast<int64_t>(shim_clock_ms * 1000); return 1; }
inline uint64_t GetTickCount64() { return static_cast<uint64_t>(shim_clock_ms); }
inline BOOL SetEvent(HANDLE) { return 1; }
inline DWORD WaitForSingleObject(HANDLE, DWORD) { return 0; }
inline void *InterlockedCompareExchangePointer(void *volatile *target, void *exchange, void *comparand) {
  return __sync_val_compare_and_swap(target, comparand, exchange);
}
inline void *InterlockedExchangePointer(void *volatile *target, void *value) {
  return __sync_lock_test_and_set(target, value);
}
