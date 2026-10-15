#ifndef APP_SHM_RING_H
#define APP_SHM_RING_H

// Linux-only SPSC byte ring in POSIX shared memory, woken by a pair of
// eventfds. The transport for hal_player's live mode: a producer process
// streams an Annex-B elementary stream (self-delimiting -- no framing on
// top), the player decodes it as it arrives.
//
// Layout of the shm object "/halcodec-shm-<name>":
//
//   +----------------+---------------------------+
//   | ShmRingHeader  |  dataBytes of raw bytes   |
//   +----------------+---------------------------+
//
// wpos/rpos are monotonic byte counters (wrapped with % dataBytes on access),
// so free = dataBytes - (wpos - rpos) and used = wpos - rpos. The producer
// writes wpos, the reader writes rpos; each loads the other's with acquire
// and stores its own with release.
//
// Wakeups are one eventfd per direction (a single eventfd shared by both
// sides deadlocks: each side's blocking read can swallow the signal meant
// for the other):
//   - dataEfd:  producer writes after appending; reader blocks in read().
//   - spaceEfd: reader writes after consuming; producer blocks in read().
// Both sides loop "check state, then block", so a pending counter is always
// backed by visible state and a stale wakeup only costs one extra pass.
//
// Rendezvous: the player creates the shm + eventfds and listens on an
// abstract unix socket; the producer connects and receives the two eventfds
// over SCM_RIGHTS in a single sendmsg. eventfd instances have no global name
// -- fd passing is the standard way to share one across unrelated processes.

#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace halcodec {

constexpr uint64_t kShmRingMagic = 0xC0DEC0DE00000001ULL;

struct ShmRingHeader {
    uint64_t magic;                 // kShmRingMagic once initialized
    uint64_t dataBytes;             // capacity of the data area
    std::atomic<uint64_t> wpos;     // producer: bytes appended (monotonic)
    std::atomic<uint64_t> rpos;     // reader: bytes consumed (monotonic)
    std::atomic<uint32_t> eof;      // producer set to 1 when done
    uint32_t reserved;
    uint8_t pad[24];                // header rounds to one 64B line
};
static_assert(sizeof(ShmRingHeader) == 64, "header must be one cache line");

namespace shm_detail {

// Sends `count` fds over an established unix socket in one SCM_RIGHTS cmsg.
inline bool SendFds(int sock, const int* fds, int count, std::string* err) {
    char dummy = 'F';
    iovec iov{&dummy, 1};
    alignas(cmsghdr) char cbuf[CMSG_SPACE(sizeof(int) * 4)];
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int) * count);
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * count);
    std::memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * count);
    if (sendmsg(sock, &msg, 0) < 0) {
        if (err) *err = std::string("sendmsg SCM_RIGHTS: ") + std::strerror(errno);
        return false;
    }
    return true;
}

// Receives `count` fds over an established unix socket.
inline bool RecvFds(int sock, int* fds, int count, std::string* err) {
    char dummy;
    iovec iov{&dummy, 1};
    alignas(cmsghdr) char cbuf[CMSG_SPACE(sizeof(int) * 4)];
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int) * 4);
    ssize_t n = recvmsg(sock, &msg, 0);
    if (n < 0) {
        if (err) *err = std::string("recvmsg SCM_RIGHTS: ") + std::strerror(errno);
        return false;
    }
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg || cmsg->cmsg_type != SCM_RIGHTS ||
        cmsg->cmsg_len < CMSG_LEN(sizeof(int) * count)) {
        if (err) *err = "expected an SCM_RIGHTS message with eventfds";
        return false;
    }
    std::memcpy(fds, CMSG_DATA(cmsg), sizeof(int) * count);
    return true;
}

// The abstract unix socket name is "\0halcodec-shm-<name>" (abstract: no
// filesystem node, auto-released when the listener closes).
inline std::string SocketPath(const std::string& name) {
    return "halcodec-shm-" + name;
}

inline void Wake(int efd) {
    uint64_t one = 1;
    ssize_t n = write(efd, &one, sizeof(one));
    (void)n;  // EAGAIN when the counter is maxed just means "already awake"
}

// Blocking one-shot clear of an eventfd. Only called after the caller was
// woken for it (epoll on the data side; the producer blocks here directly).
inline void Await(int efd) {
    uint64_t v;
    ssize_t n = read(efd, &v, sizeof(v));
    (void)n;
}

// True when the peer socket has closed (the other side died mid-stream --
// without this, a waiter blocks on the eventfd forever).
inline bool PeerDead(int sockFd) {
    char c;
    ssize_t n = recv(sockFd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0) return true;  // orderly EOF
    return n < 0 && errno != EAGAIN && errno != EWOULDBLOCK;  // reset
}

// Blocks until the eventfd fires or the peer socket closes. Returns true if
// the peer is gone; otherwise the eventfd was cleared and the caller
// re-checks the ring state (a stale wakeup only costs one extra pass).
inline bool AwaitOrPeer(int efd, int peerFd) {
    for (;;) {
        pollfd pfd[2] = {{efd, POLLIN, 0}, {peerFd, POLLIN, 0}};
        int rc = poll(pfd, 2, -1);
        if (rc < 0 && errno == EINTR) continue;
        if (PeerDead(peerFd)) return true;
        if (pfd[0].revents & POLLIN) {
            Await(efd);
            return false;
        }
        if (pfd[1].revents) return true;  // peer event without bytes = dead
        return false;
    }
}

}  // namespace shm_detail

// Reader/player side. create() makes the shm object (re-initializing a stale
// one from a crashed run: wpos/rpos reset to 0, so a leftover producer's
// in-flight bytes are discarded -- the name is exclusive to one player),
// opens the two eventfds and starts listening; acceptProducer() blocks until
// the producer connects and hands it the wakeup fds.
class ShmRingHost {
public:
    ~ShmRingHost() {
        if (shmFd_ >= 0) {
            shm_unlink(shmName_.c_str());  // best effort; a SIGKILL leaves the
                                           // object, the next create() re-inits
        }
        for (int fd : {dataEfd_, spaceEfd_, listenFd_, connFd_}) {
            if (fd >= 0) close(fd);
        }
    }

    bool create(const std::string& name, uint64_t dataBytes, std::string* err) {
        shmName_ = "/halcodec-shm-" + name;
        dataBytes_ = dataBytes;
        shmFd_ = shm_open(shmName_.c_str(), O_CREAT | O_RDWR, 0600);
        if (shmFd_ < 0) {
            if (err) *err = std::string("shm_open: ") + std::strerror(errno);
            return false;
        }
        if (ftruncate(shmFd_, static_cast<off_t>(sizeof(ShmRingHeader) + dataBytes)) != 0) {
            if (err) *err = std::string("ftruncate: ") + std::strerror(errno);
            return false;
        }
        void* mem = mmap(nullptr, sizeof(ShmRingHeader) + dataBytes,
                         PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
        if (mem == MAP_FAILED) {
            if (err) *err = std::string("mmap: ") + std::strerror(errno);
            return false;
        }
        map_ = static_cast<uint8_t*>(mem);
        mapLen_ = sizeof(ShmRingHeader) + dataBytes;
        h_ = reinterpret_cast<ShmRingHeader*>(map_);
        data_ = map_ + sizeof(ShmRingHeader);
        // Field-by-field: std::atomic members make the header non-assignable,
        // and a stale ring from a crashed run must start from zero.
        h_->magic = kShmRingMagic;
        h_->dataBytes = dataBytes;
        h_->wpos.store(0, std::memory_order_relaxed);
        h_->rpos.store(0, std::memory_order_relaxed);
        h_->eof.store(0, std::memory_order_relaxed);
        h_->reserved = 0;
        std::memset(h_->pad, 0, sizeof(h_->pad));

        dataEfd_ = eventfd(0, 0);
        spaceEfd_ = eventfd(0, 0);
        if (dataEfd_ < 0 || spaceEfd_ < 0) {
            if (err) *err = std::string("eventfd: ") + std::strerror(errno);
            return false;
        }

        listenFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            if (err) *err = std::string("socket: ") + std::strerror(errno);
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::string path = shm_detail::SocketPath(name);
        std::memcpy(addr.sun_path + 1, path.c_str(), path.size());  // abstract
        socklen_t len = offsetof(sockaddr_un, sun_path) + 1 + path.size();
        if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), len) != 0 ||
            listen(listenFd_, 1) != 0) {
            if (err) *err = std::string("bind/listen: ") + std::strerror(errno) +
                            " (another player on shm:" + name + "?)";
            return false;
        }
        return true;
    }

    // Blocks until the producer connects, then sends it the eventfds.
    bool acceptProducer(std::string* err) {
        connFd_ = accept(listenFd_, nullptr, nullptr);
        if (connFd_ < 0) {
            if (err) *err = std::string("accept: ") + std::strerror(errno);
            return false;
        }
        int fds[2] = {dataEfd_, spaceEfd_};
        return shm_detail::SendFds(connFd_, fds, 2, err);
    }

    // Returns >0 bytes copied, 0 on end of stream (producer finished and the
    // ring drained), blocks otherwise. Caller-owned buffer.
    int64_t read(uint8_t* dst, uint64_t max) {
        for (;;) {
            uint64_t w = h_->wpos.load(std::memory_order_acquire);
            uint64_t r = h_->rpos.load(std::memory_order_relaxed);  // ours alone
            if (w > r) {
                uint64_t n = std::min<uint64_t>(max, w - r);
                uint64_t off = r % dataBytes_;
                uint64_t first = std::min<uint64_t>(n, dataBytes_ - off);
                std::memcpy(dst, data_ + off, first);
                std::memcpy(dst + first, data_, n - first);
                h_->rpos.store(r + n, std::memory_order_release);
                shm_detail::Wake(spaceEfd_);
                return static_cast<int64_t>(n);
            }
            if (h_->eof.load(std::memory_order_acquire)) return 0;
            if (shm_detail::AwaitOrPeer(dataEfd_, connFd_)) return 0;  // producer died
        }
    }

    // Reader-initiated teardown (e.g. the window closed while the producer
    // still streams): releases the reader blocked in read(). The producer
    // notices on its own socket.
    void abort() {
        h_->eof.store(1, std::memory_order_release);
        shm_detail::Wake(dataEfd_);
    }

private:
    uint8_t* map_ = nullptr;
    size_t mapLen_ = 0;
    ShmRingHeader* h_ = nullptr;
    uint8_t* data_ = nullptr;
    std::string shmName_;
    uint64_t dataBytes_ = 0;
    int dataEfd_ = -1;
    int spaceEfd_ = -1;
    int listenFd_ = -1;
    int connFd_ = -1;
    int shmFd_ = -1;
};

// Producer side: connect to the player, take the eventfds, then write().
class ShmRingWriter {
public:
    ~ShmRingWriter() {
        for (int fd : {dataEfd_, spaceEfd_, sockFd_, shmFd_}) {
            if (fd >= 0) close(fd);
        }
        if (map_) munmap(map_, mapLen_);
    }

    bool connect(const std::string& name, std::string* err) {
        sockFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (sockFd_ < 0) {
            if (err) *err = std::string("socket: ") + std::strerror(errno);
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::string path = shm_detail::SocketPath(name);
        std::memcpy(addr.sun_path + 1, path.c_str(), path.size());
        socklen_t len = offsetof(sockaddr_un, sun_path) + 1 + path.size();
        if (::connect(sockFd_, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
            if (err) *err = std::string("connect halcodec-shm-") + name + ": " +
                            std::strerror(errno) + " (player not running?)";
            return false;
        }
        int fds[2];
        if (!shm_detail::RecvFds(sockFd_, fds, 2, err)) return false;
        dataEfd_ = fds[0];
        spaceEfd_ = fds[1];

        std::string shmName = "/halcodec-shm-" + name;
        shmFd_ = shm_open(shmName.c_str(), O_RDWR, 0600);
        if (shmFd_ < 0) {
            if (err) *err = std::string("shm_open: ") + std::strerror(errno);
            return false;
        }
        ShmRingHeader probe{};
        if (pread(shmFd_, &probe, sizeof(probe), 0) != sizeof(probe) ||
            probe.magic != kShmRingMagic) {
            if (err) *err = "shm object is not a live HALCodec ring";
            return false;
        }
        mapLen_ = sizeof(ShmRingHeader) + probe.dataBytes;
        void* mem = mmap(nullptr, mapLen_, PROT_READ | PROT_WRITE, MAP_SHARED,
                         shmFd_, 0);
        if (mem == MAP_FAILED) {
            if (err) *err = std::string("mmap: ") + std::strerror(errno);
            return false;
        }
        map_ = static_cast<uint8_t*>(mem);
        h_ = reinterpret_cast<ShmRingHeader*>(map_);
        data_ = map_ + sizeof(ShmRingHeader);
        dataBytes_ = probe.dataBytes;
        return true;
    }

    // Appends n bytes, blocking while the ring is full (the reader's
    // space-event wakes it). n may exceed the ring: chunked internally.
    bool write(const uint8_t* p, uint64_t n) {
        while (n > 0) {
            uint64_t chunk = std::min<uint64_t>(n, dataBytes_ / 2);
            for (;;) {
                uint64_t r = h_->rpos.load(std::memory_order_acquire);
                uint64_t w = h_->wpos.load(std::memory_order_relaxed);  // ours
                if (dataBytes_ - (w - r) >= chunk) break;
                if (shm_detail::AwaitOrPeer(spaceEfd_, sockFd_)) return false;
            }
            uint64_t w = h_->wpos.load(std::memory_order_relaxed);
            uint64_t off = w % dataBytes_;
            uint64_t first = std::min<uint64_t>(chunk, dataBytes_ - off);
            std::memcpy(data_ + off, p, first);
            std::memcpy(data_, p + first, chunk - first);  // wrap: back to start
            h_->wpos.store(w + chunk, std::memory_order_release);
            shm_detail::Wake(dataEfd_);
            p += chunk;
            n -= chunk;
        }
        return true;
    }

    // The stream is complete: set the flag first, then wake the reader.
    void finish() {
        h_->eof.store(1, std::memory_order_release);
        shm_detail::Wake(dataEfd_);
    }

private:
    uint8_t* map_ = nullptr;
    size_t mapLen_ = 0;
    ShmRingHeader* h_ = nullptr;
    uint8_t* data_ = nullptr;
    uint64_t dataBytes_ = 0;
    int dataEfd_ = -1;
    int spaceEfd_ = -1;
    int sockFd_ = -1;
    int shmFd_ = -1;
};

}  // namespace halcodec

#endif  // APP_SHM_RING_H
