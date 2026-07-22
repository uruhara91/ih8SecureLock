#include "binder.hpp"

#include <android/log.h>
#include <asm-generic/fcntl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define LOGD(fmt, ...) \
    __android_log_print(ANDROID_LOG_DEBUG, "ih8SecureLock", "[%d] " fmt, __LINE__, ##__VA_ARGS__)

bool getMapping(const char* lib_name, ino_t* inode, dev_t* dev) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;
    char mapbuf[256], flags[8];
    size_t lib_name_len = strlen(lib_name);
    while (fgets(mapbuf, sizeof(mapbuf), fp)) {
        unsigned int dev_major, dev_minor;
        int cur = 0;
        // %7s is bounded to sizeof(flags)-1 so a malformed/unexpected line
        // (e.g. no whitespace where expected) can never overflow `flags`.
        // %n is only written if every prior conversion succeeded, so on a
        // partial/failed match `cur` stays 0 and the length check below
        // safely skips the line instead of reading before mapbuf[0].
        sscanf(mapbuf, "%*s %7s %*x %x:%x %lu %*s%n", flags, &dev_major, &dev_minor, inode, &cur);
        if ((size_t)cur < lib_name_len) continue;
        if (memcmp(&mapbuf[(size_t)cur - lib_name_len], lib_name, lib_name_len) == 0 && flags[2] == 'x') {
            *dev = makedev(dev_major, dev_minor);
            fclose(fp);
            return true;
        }
    }
    fclose(fp);
    return false;
}

uint32_t getStaticIntFieldJni(JNIEnv* env, const char* cls_name, const char* field_name) {
    jclass cls = env->FindClass(cls_name);
    if (cls == nullptr) {
        env->ExceptionClear();
        LOGD("ERROR getStaticIntFieldJni: Could not get class '%s'", cls_name);
        return 0;
    }
    jfieldID field = env->GetStaticFieldID(cls, field_name, "I");
    if (field == nullptr) {
        env->ExceptionClear();
        LOGD("ERROR getStaticIntFieldJni: Could not get field %s.%s", cls_name, field_name);
        return 0;
    }
    jint val = env->GetStaticIntField(cls, field);
    return val;
}

void companionSendFile(const char* path, int remote_fd) {
    off_t size = 0;
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        LOGD("ERROR open: %s", strerror(errno));
    } else {
        struct stat st;
        if (fstat(fd, &st) == -1) {
            LOGD("ERROR fstat: %s", strerror(errno));
        } else {
            size = st.st_size;
        }
    }

    // Protocol: always send the size first (0 on any failure above) so the
    // remote side has a well-defined framing to read, even on the error path.
    if (write(remote_fd, &size, sizeof(size)) < 0) {
        LOGD("ERROR write: %s", strerror(errno));
        size = 0;
    }
    // fd 0 is a perfectly valid descriptor (open() can return it if fd 0
    // happened to be free); the original `fd > 0` check would leak it.
    if (fd >= 0) {
        if (size > 0 && sendfile(remote_fd, fd, NULL, size) < 0) {
            LOGD("ERROR sendfile: %s", strerror(errno));
        }
        close(fd);
    }
}

bool readFullFromFd(int fd, void* buf, off_t size) {
    off_t size_read = 0;
    while (size_read < size) {
        ssize_t ret = read(fd, reinterpret_cast<char*>(buf) + size_read, size - size_read);
        if (ret < 0) {
            if (errno == EINTR) continue;  // interrupted, safe to retry
            LOGD("ERROR read: %s", strerror(errno));
            return false;
        } else if (ret == 0) {
            // EOF / peer closed before delivering the promised `size` bytes.
            // The original code treated this as "add zero and loop again",
            // which spins the CPU forever instead of failing.
            LOGD("ERROR read: unexpected EOF (%ld/%ld bytes)", (long)size_read, (long)size);
            return false;
        } else {
            size_read += ret;
        }
    }
    return true;
}
