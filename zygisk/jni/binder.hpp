#pragma once

#include <jni.h>
#include <stdint.h>
#include <sys/types.h>

#if defined(__LP64__)
#define FLAT_BINDER_OBJ_SIZE 24
#else
#define FLAT_BINDER_OBJ_SIZE 16
#endif

#define STUB(n) (n "$Stub")
#define TRSCTN(n) ("TRANSACTION_" n)

struct PParcel {
    // Layout must mirror the real android::Parcel object we alias onto via
    // pointer-cast in module.cpp, so `error` stays even though nothing here
    // reads it directly.
    // cppcheck-suppress unusedStructMember
    size_t error;
    // Read as pparcel->data in module.cpp's transactHook(); cppcheck checks
    // this header on its own and can't see that cross-TU use.
    // cppcheck-suppress unusedStructMember
    char* data;
    // Read as pparcel->data_size in module.cpp's transactHook(); same
    // cross-TU reason as `data` above.
    // cppcheck-suppress unusedStructMember
    size_t data_size;
};

// Bounds-checked cursor over a raw Parcel buffer.
//
// The original implementation trusted the caller to pre-compute how many
// bytes were left before every skip/read, which is easy to get wrong and
// turns a single miscalculation into an out-of-bounds read on attacker- or
// otherwise-unexpectedly-shaped IPC data. This version tracks the buffer
// length itself and "poisons" the parcel (valid = false) the moment any
// operation would run past the end, after which every further op is a
// cheap no-op that returns 0/nullptr instead of touching memory. Callers
// only need to check ok() (or a null return) once, at the point they're
// about to use the result.
struct FakeParcel {
   private:
    char* data;
    uint32_t cur;
    size_t size;
    bool valid;

    // Returns true iff reading/skipping `n` more bytes from `cur` stays
    // within `size`. Uses uint64_t so the check itself can't overflow on
    // 32-bit ABIs (armeabi-v7a/x86) where size_t is only 32 bits.
    inline bool ensure(uint64_t n) {
        if (!valid || (uint64_t)cur + n > (uint64_t)size) {
            valid = false;
            return false;
        }
        return true;
    }

   public:
    FakeParcel(char* data, size_t size);

    inline bool ok() const { return valid; }
    void skip(uint32_t n);
    uint32_t getCursor();
    void skipFlatObj();

    uint32_t* peekInt32Ref();
    uint32_t readInt32();
    char16_t* readString16(uint32_t len);
};

inline FakeParcel::FakeParcel(char* data, size_t size)
    : data(data), cur(0), size(size), valid(data != nullptr) {}

inline void FakeParcel::skip(uint32_t n) {
    if (ensure(n)) cur += n;
}
inline uint32_t FakeParcel::getCursor() { return cur; }
inline void FakeParcel::skipFlatObj() { skip(FLAT_BINDER_OBJ_SIZE); }

inline uint32_t* FakeParcel::peekInt32Ref() {
    if (!ensure(sizeof(uint32_t))) return nullptr;
    return (uint32_t*)(data + cur);
}

inline uint32_t FakeParcel::readInt32() {
    const uint32_t* p = peekInt32Ref();
    if (!p) return 0;
    cur += sizeof(uint32_t);
    return *p;
}

inline char16_t* FakeParcel::readString16(uint32_t len) {
    // len+1 for the trailing null u16
    uint64_t bytes = (uint64_t)len * sizeof(char16_t) + sizeof(char16_t);
    if (!ensure(bytes)) return nullptr;
    char16_t* s = (char16_t*)(data + cur);
    cur += (uint32_t)bytes;
    return s;
}

inline size_t getBinderHeadersLen(int sdk) {
    if (sdk >= 30) return 3 * sizeof(uint32_t);
    else if (sdk == 29) return 2 * sizeof(uint32_t);
    else return 1 * sizeof(uint32_t);
}

bool getMapping(const char* lib, ino_t* inode, dev_t* dev);

uint32_t getStaticIntFieldJni(JNIEnv* env, const char* cls_name, const char* field_name);

// NOTE: not currently called anywhere in this module (no companion process is
// used - all hooking happens in-process). Kept as a general-purpose utility
// for anyone extending the module with root-companion IPC; bounds/EOF-hardened
// below so it's safe to reach for later.
void companionSendFile(const char* path, int remote_fd);

bool readFullFromFd(int fd, void* buf, off_t size);