#ifndef APP_SHM_RING_H
#define APP_SHM_RING_H

// SPSC byte ring in POSIX shared memory, woken by a pair of event channels
// (Linux + macOS). The transport for hal_player's live mode: a producer
// process streams an Annex-B elementary stream (self-delimiting -- no
// framing on top), the player decodes it as it arrives.
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
// Wakeups are one channel per direction (a single channel shared by both
// sides deadlocks: each side's blocking read can swallow the signal meant
// for the other):
//   - data:  producer signals after appending; reader blocks on it.
//   - space: reader signals after consuming; producer blocks on it.
// Linux implements each as an eventfd shared by both sides over fd passing.
// macOS has no eventfd: each direction is a non-blocking pipe instead, the
// "signal" is one byte, and the read side drains after poll(). Both sides
// loop "check state, then block", so a pending signal is always backed by
// visible state and a stale wakeup only costs one extra pass.
//
// Rendezvous: the player creates the shm + wakeup channels and listens;
// the producer connects and receives the two write/await ends over
// SCM_RIGHTS in a single sendmsg (eventfds have no global name, and on
// macOS only one pipe end is useful to each side -- fd passing is the
// standard way to share them across unrelated processes).
//
// The rendezvous socket is an abstract unix socket on Linux ("\0halcodec-
// shm-<name>": no filesystem node, auto-released on close). macOS has no
// abstract namespace, so it binds a path-based socket in /tmp instead and
// probes/recovers a stale node left by a crashed run.

#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/eventfd.h>
#endif

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

// The POSIX shm object name. macOS caps shm names at PSHMNAMLEN (31) chars
// including the leading slash, so it gets a shorter prefix (leaves 21 chars
// for <name>); Linux keeps the descriptive one.
inline std::string ShmName(const std::string& name) {
#if defined(__linux__)
    return "/halcodec-shm-" + name;
#else
    return "/halc-shm-" + name;
#endif
}

// Linux rendezvous is an abstract unix socket named "\0halcodec-shm-<name>"
// (abstract: no filesystem node, auto-released when the listener closes).
// macOS has no abstract namespace -- bind a path-based socket in /tmp.
#if defined(__linux__)
inline std::string SocketPath(const std::string& name) {
    return "halcodec-shm-" + name;
}
#else
constexpr size_t kMacShmNameMax = 31;  // PSHMNAMLEN, slash included

inline std::string SocketPath(const std::string& name) {
    return std::string("/tmp/halcodec-shm-") + name + ".sock";
}

// True when something is already listening on the path socket (a live
// player owns the name -- vs a stale node left by a crashed run).
inline bool ProbeLive(const std::string& path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    close(fd);
    return rc == 0;
}
#endif

// macOS wakeup pipes are non-blocking: Wake tolerates a full buffer (a
// dropped token can't lose a wakeup -- the queued tokens already guarantee
// one), and Await drains after poll().
inline void SetNonBlocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

inline void Wake(int efd) {
#if defined(__linux__)
    uint64_t one = 1;
#else
    uint8_t one = 1;
#endif
    ssize_t n = write(efd, &one, sizeof(one));
    (void)n;  // EAGAIN (counter maxed / pipe full) just means "already awake"
}

// Clears one wakeup. Only called after the caller was woken for it (poll
// on the channel's read side). Linux: an eventfd read resets the counter.
// macOS: drain every queued byte (the fd is non-blocking, so the loop ends
// on EAGAIN once the backlog is consumed).
inline void Await(int efd) {
#if defined(__linux__)
    uint64_t v;
    ssize_t n = read(efd, &v, sizeof(v));
    (void)n;
#else
    uint8_t buf[64];
    while (read(efd, buf, sizeof(buf)) > 0) {}
#endif
}

// True when the peer socket has closed (the other side died mid-stream --
// without this, a waiter blocks on its wakeup channel forever).
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
// opens the two wakeup channels and starts listening; acceptProducer()
// blocks until the producer connects and hands it the channel ends.
class ShmRingHost {
public:
    ~ShmRingHost() {
        if (shmFd_ >= 0) {
            shm_unlink(shmName_.c_str());  // best effort; a SIGKILL leaves the
                                           // object, the next create() re-inits
        }
#if !defined(__linux__)
        if (!sockPath_.empty()) ::unlink(sockPath_.c_str());  // socket node
#endif
        auto closeFd = [](int fd) { if (fd >= 0) close(fd); };
#if defined(__linux__)
        closeFd(dataPoll_);   // == dataWake_ (same eventfd)
        closeFd(spaceWake_);  // == spaceGive_ (same eventfd)
#else
        closeFd(dataPoll_);
        closeFd(dataWake_);
        closeFd(spaceWake_);
        closeFd(spaceGive_);
#endif
        closeFd(listenFd_);
        closeFd(connFd_);
    }

    bool create(const std::string& name, uint64_t dataBytes, std::string* err) {
        shmName_ = shm_detail::ShmName(name);
#if !defined(__linux__)
        if (shmName_.size() > shm_detail::kMacShmNameMax) {
            if (err) *err = "shm name '" + name +
                            "' too long for macOS (<= 21 chars)";
            return false;
        }
#endif
        dataBytes_ = dataBytes;
        shmFd_ = shm_open(shmName_.c_str(), O_CREAT | O_RDWR, 0600);
        if (shmFd_ < 0) {
            if (err) *err = std::string("shm_open: ") + std::strerror(errno);
            return false;
        }
        struct stat st{};
        if (fstat(shmFd_, &st) != 0) {
            if (err) *err = std::string("fstat: ") + std::strerror(errno);
            return false;
        }
        // Darwin allows exactly one ftruncate on a POSIX shm object (growing
        // it from zero); later calls -- same size or shrink -- fail EINVAL.
        // A reused stale object therefore keeps its size, which is fine: both
        // sides take the mapping length from the header's dataBytes.
        const off_t need =
            static_cast<off_t>(sizeof(ShmRingHeader) + dataBytes);
        if (st.st_size < need && ftruncate(shmFd_, need) != 0) {
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

#if defined(__linux__)
        int dataEfd = eventfd(0, 0);
        int spaceEfd = eventfd(0, 0);
        if (dataEfd < 0 || spaceEfd < 0) {
            if (err) *err = std::string("eventfd: ") + std::strerror(errno);
            return false;
        }
        dataPoll_ = dataWake_ = dataEfd;     // one eventfd: producer signals, we poll
        spaceWake_ = spaceGive_ = spaceEfd;  // one eventfd: we signal, producer polls
#else
        // macOS has no eventfd: one non-blocking pipe per direction.
        int dataPipe[2], spacePipe[2];
        if (pipe(dataPipe) != 0 || pipe(spacePipe) != 0) {
            if (err) *err = std::string("pipe: ") + std::strerror(errno);
            return false;
        }
        for (int fd : {dataPipe[0], dataPipe[1], spacePipe[0], spacePipe[1]})
            shm_detail::SetNonBlocking(fd);
        dataPoll_ = dataPipe[0];    // producer's append signal arrives here
        dataWake_ = dataPipe[1];    // kept copy: abort() self-wake (also sent)
        spaceWake_ = spacePipe[1];  // consumed: wake the producer
        spaceGive_ = spacePipe[0];  // producer's await end (sent)
#endif

        listenFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            if (err) *err = std::string("socket: ") + std::strerror(errno);
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::string path = shm_detail::SocketPath(name);
#if defined(__linux__)
        std::memcpy(addr.sun_path + 1, path.c_str(), path.size());  // abstract
        socklen_t len = offsetof(sockaddr_un, sun_path) + 1 + path.size();
#else
        // A live player answers the probe; a crashed one only leaves the node.
        if (shm_detail::ProbeLive(path)) {
            if (err) *err = "another player already owns shm:" + name;
            return false;
        }
        ::unlink(path.c_str());  // stale node from a crashed run
        sockPath_ = path;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        socklen_t len = sizeof(addr);
#endif
        if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), len) != 0 ||
            listen(listenFd_, 1) != 0) {
            if (err) *err = std::string("bind/listen: ") + std::strerror(errno) +
                            " (another player on shm:" + name + "?)";
            return false;
        }
        return true;
    }

    // Blocks until the producer connects, then hands it the wakeup channel
    // ends: the data-signal end it writes after appending, and the space
    // end it blocks on while the ring is full.
    bool acceptProducer(std::string* err) {
        connFd_ = accept(listenFd_, nullptr, nullptr);
        if (connFd_ < 0) {
            if (err) *err = std::string("accept: ") + std::strerror(errno);
            return false;
        }
        int fds[2] = {dataWake_, spaceGive_};
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
                shm_detail::Wake(spaceWake_);
                return static_cast<int64_t>(n);
            }
            if (h_->eof.load(std::memory_order_acquire)) return 0;
            if (shm_detail::AwaitOrPeer(dataPoll_, connFd_)) {
                // Peer gone -- but bytes it appended before dying (or before
                // exiting normally) may have raced in while we waited. Give
                // up only once the ring is drained.
                if (h_->wpos.load(std::memory_order_acquire) > r) continue;
                return 0;
            }
        }
    }

    // Reader-initiated teardown (e.g. the window closed while the producer
    // still streams): releases the reader blocked in read(). The producer
    // notices on its own socket.
    void abort() {
        h_->eof.store(1, std::memory_order_release);
        shm_detail::Wake(dataWake_);
    }

private:
    // One wakeup channel per direction (see the file comment). Linux: one
    // eventfd per channel shared with the producer over fd passing, so
    // dataWake_ == dataPoll_ and spaceGive_ == spaceWake_ alias the same
    // descriptor. macOS: a non-blocking pipe per channel -- the host keeps
    // the data-read end (poll) plus a copy of its write end (abort() wakes
    // the reader from another thread) and the space-write end; the producer
    // is handed dataWake_ (append signal) and spaceGive_ (await end).
    int dataPoll_ = -1;
    int dataWake_ = -1;
    int spaceWake_ = -1;
    int spaceGive_ = -1;
    uint8_t* map_ = nullptr;
    size_t mapLen_ = 0;
    ShmRingHeader* h_ = nullptr;
    uint8_t* data_ = nullptr;
    std::string shmName_;
#if !defined(__linux__)
    std::string sockPath_;  // macOS: filesystem rendezvous socket
#endif
    uint64_t dataBytes_ = 0;
    int listenFd_ = -1;
    int connFd_ = -1;
    int shmFd_ = -1;
};

// Producer side: connect to the player, take the wakeup channel ends, then
// write().
class ShmRingWriter {
public:
    ~ShmRingWriter() {
        for (int fd : {dataWake_, spacePoll_, sockFd_, shmFd_}) {
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
#if defined(__linux__)
        std::memcpy(addr.sun_path + 1, path.c_str(), path.size());
        socklen_t len = offsetof(sockaddr_un, sun_path) + 1 + path.size();
#else
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        socklen_t len = sizeof(addr);
#endif
        if (::connect(sockFd_, reinterpret_cast<sockaddr*>(&addr), len) != 0) {
            if (err) *err = std::string("connect ") + path + ": " +
                            std::strerror(errno) + " (player not running?)";
            return false;
        }
        int fds[2];
        if (!shm_detail::RecvFds(sockFd_, fds, 2, err)) return false;
        dataWake_ = fds[0];
        spacePoll_ = fds[1];
#if !defined(__linux__)
        shm_detail::SetNonBlocking(dataWake_);
        shm_detail::SetNonBlocking(spacePoll_);
#endif

        std::string shmName = shm_detail::ShmName(name);
#if !defined(__linux__)
        if (shmName.size() > shm_detail::kMacShmNameMax) {
            if (err) *err = "shm name '" + name +
                            "' too long for macOS (<= 21 chars)";
            return false;
        }
#endif
        shmFd_ = shm_open(shmName.c_str(), O_RDWR, 0600);
        if (shmFd_ < 0) {
            if (err) *err = std::string("shm_open: ") + std::strerror(errno);
            return false;
        }
        ShmRingHeader probe{};
        {
            // Probe the header. macOS shm objects don't support pread/lseek
            // (ESPIPE) -- mmap is the only way to read one. fstat guards the
            // mapping against a too-small (non-ring) object.
            struct stat st{};
            if (fstat(shmFd_, &st) != 0 ||
                st.st_size < static_cast<off_t>(sizeof(ShmRingHeader))) {
                if (err) *err = "shm object is not a live HALCodec ring";
                return false;
            }
            void* head = mmap(nullptr, sizeof(ShmRingHeader),
                              PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
            if (head == MAP_FAILED) {
                if (err) *err = std::string("mmap: ") + std::strerror(errno);
                return false;
            }
            std::memcpy(&probe, head, sizeof(probe));
            munmap(head, sizeof(ShmRingHeader));
        }
        if (probe.magic != kShmRingMagic) {
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
                if (shm_detail::AwaitOrPeer(spacePoll_, sockFd_)) return false;
            }
            uint64_t w = h_->wpos.load(std::memory_order_relaxed);
            uint64_t off = w % dataBytes_;
            uint64_t first = std::min<uint64_t>(chunk, dataBytes_ - off);
            std::memcpy(data_ + off, p, first);
            std::memcpy(data_, p + first, chunk - first);  // wrap: back to start
            h_->wpos.store(w + chunk, std::memory_order_release);
            shm_detail::Wake(dataWake_);
            p += chunk;
            n -= chunk;
        }
        return true;
    }

    // The stream is complete: set the flag first, then wake the reader.
    void finish() {
        h_->eof.store(1, std::memory_order_release);
        shm_detail::Wake(dataWake_);
    }

private:
    // The received ends of the host's two wakeup channels: dataWake_ is
    // signalled after appending (wakes the reader), spacePoll_ is blocked
    // on while the ring is full (the reader signals consumed space).
    uint8_t* map_ = nullptr;
    size_t mapLen_ = 0;
    ShmRingHeader* h_ = nullptr;
    uint8_t* data_ = nullptr;
    uint64_t dataBytes_ = 0;
    int dataWake_ = -1;
    int spacePoll_ = -1;
    int sockFd_ = -1;
    int shmFd_ = -1;
};

}  // namespace halcodec

#endif  // APP_SHM_RING_H
