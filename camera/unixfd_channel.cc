// unixfd_channel.cc — Linux 跨进程通道,层 2 的一个 backend。
//
// 用途:v4l2 / 其它 host 帧源 → 多个消费者进程,零拷贝。这就是"相机数据同时给
// 多个进程处理"在 Linux 上的落地:一个生产进程采集,若干消费进程各自订阅。
//
// 传输:
//   - 环本体放在一个 memfd(匿名内存文件)里:header + 每槽元数据 + 像素区。
//   - 生产者在抽象 AF_UNIX 域(SOCK_SEQPACKET)上 listen;每个消费者 connect 一次,
//     生产者用 SCM_RIGHTS 把那一个 memfd 传过去(只传一次,不是每帧)。
//   - 之后每帧只是一条 {slot, seq, pts} 小消息;像素消费者直接读自己 mmap 的环,
//     真正零拷贝(不跨进程复制像素,只跨进程共享同一段物理页)。
//
// 生命周期:每槽一个 64 位"持有者位图",bit63 = 生产者预留,bit0..62 = 消费者序号。
// 槽空闲 ⇔ 位图 == 0。消费者收到帧时它的位已被生产者置上;消费者释放 ReceivedFrame
// 时清掉自己的位;位图归零即可被生产者复用。消费者异常退出(位没清)由生产者的
// accept 线程在 POLLHUP 时补清 —— 故一个进程死掉不会把槽永久锁死。
//
// 错帧策略:消费者 socket 满(sendmsg EAGAIN)→ 丢弃该消费者这一帧(清它的位),
// 生产者绝不阻塞。与 loopback 的 drop-oldest 同义,只是这里按消费者丢弃。
//
// 平台:Linux 专用(AF_UNIX SOCK_SEQPACKET + SCM_RIGHTS + memfd)。非 Linux 平台
// 不编译本文件,kind "unixfd" 不存在,工厂会明确报错。

#include "frame_channel.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace camera {

namespace {

// --- 共享环布局 -----------------------------------------------------------
//
//   [ ShmHeader ][ SlotMeta × numSlots ][ 像素区:slotBytes × numSlots ]
//
// 两侧都按同一函数算出偏移,header 里再冗余放一份(pixelOffset)以便自校验。
constexpr uint64_t kMagic = 0x48414c4348434e31ULL;  // "HALCHCN1"
constexpr uint32_t kVersion = 1;
constexpr int kMaxConsumers = 63;  // bit0..62;bit63 留给"生产者预留"
constexpr uint64_t kReservedBit = 1ULL << 63;

struct SlotMeta {
    uint64_t holders;  // bitmask,见上
    uint64_t seq;      // 最近写入本槽的帧序号(诊断)
};

struct ShmHeader {
    uint64_t magic;
    uint32_t version;
    uint32_t numSlots;
    uint64_t slotBytes;
    uint32_t format;      // halcodec::PixelFormat
    uint32_t width;
    uint32_t height;
    uint32_t producerAlive;  // 0 = 流已结束
    uint64_t strides[4];
    uint64_t slotTableOffset;
    uint64_t pixelOffset;
    uint64_t totalBytes;
};

inline uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

struct Layout {
    uint64_t slotTable;
    uint64_t pixels;
    uint64_t total;
};
Layout ComputeLayout(int numSlots, uint64_t slotBytes) {
    Layout l;
    l.slotTable = AlignUp(sizeof(ShmHeader), 64);
    l.pixels = AlignUp(l.slotTable + sizeof(SlotMeta) * numSlots, 4096);
    l.total = l.pixels + slotBytes * numSlots;
    return l;
}

size_t SlotBytesFor(halcodec::PixelFormat f, int w, int h) {
    const size_t px = static_cast<size_t>(w) * static_cast<size_t>(h);
    switch (f) {
        case halcodec::PixelFormat::NV12:
        case halcodec::PixelFormat::I420:
            return px * 3 / 2;
        case halcodec::PixelFormat::P010:
        case halcodec::PixelFormat::P016:
        case halcodec::PixelFormat::NV16:
        case halcodec::PixelFormat::P210:
            return px * 2;
        case halcodec::PixelFormat::GRAY:
            return px;
        default:
            return px * 3 / 2;
    }
}

// --- 逐消费者控制消息 -----------------------------------------------------
enum : uint32_t { kMsgHello = 1, kMsgFrame = 2, kMsgClose = 3 };
struct Msg {
    uint32_t type;
    uint32_t index;  // HELLO: 本消费者的位序号;FRAME: 槽号
    uint64_t seq;    // FRAME: 帧序号
    int64_t pts;     // FRAME: 时间戳
};

std::string SocketPath(const std::string& name) {
    // 抽象命名空间:首字节 0,不落文件系统,免清理、免权限问题。
    return std::string("\0halcodec.ch.", 13) + name;
}

// 一块环在本进程的视图。生产者与消费者各持一个;shared_ptr 保证映射在仍有
// ReceivedFrame 引用时不被解映射。
struct UfdRegion {
    void* base = nullptr;
    size_t size = 0;
    ShmHeader* hdr = nullptr;
    bool producer = false;
    int consumerIndex = -1;  // 消费者:自己的位序号
    int sock = -1;           // 消费者:与本生产者的连接

    ~UfdRegion() {
        if (sock >= 0) ::close(sock);
        if (base) ::munmap(base, size);
    }

    uint8_t* slotPixels(uint32_t slot) {
        return static_cast<uint8_t*>(base) + hdr->pixelOffset +
               hdr->slotBytes * slot;
    }
    SlotMeta* slotMeta(uint32_t slot) {
        return reinterpret_cast<SlotMeta*>(static_cast<uint8_t*>(base) +
                                           hdr->slotTableOffset) + slot;
    }
};

// --- 工厂 ----------------------------------------------------------------

int MakeMemfd(const std::string& name) {
    // memfd 首选;没有则退回一个立即 unlink 的 shm_open 名。
#ifdef MFD_CLOEXEC
    int mfd = ::memfd_create(("halcodec-" + name).c_str(), MFD_CLOEXEC);
    if (mfd >= 0) return mfd;
#endif
    std::string shm = "/halcodec-" + name + "-" + std::to_string(::getpid());
    int fd = ::shm_open(shm.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) ::shm_unlink(shm.c_str());
    return fd;
}

// consumer 取到的一帧:指向共享环某槽;析构时清掉自己的持有位。定义在
// UnixFdChannel 之前,因为 Subscribe 要在这里构造成 ReceivedFrame 基类。
class UfdReceived : public ReceivedFrame {
public:
    UfdReceived(std::shared_ptr<UfdRegion> r, uint32_t slot, uint64_t seq,
                int64_t pts)
        : region_(std::move(r)), slot_(slot) {
        const ShmHeader* h = region_->hdr;
        desc_.locality = halcodec::FrameLocality::Host;
        desc_.format = static_cast<halcodec::PixelFormat>(h->format);
        desc_.width = static_cast<int>(h->width);
        desc_.height = static_cast<int>(h->height);
        for (int i = 0; i < 4; ++i) desc_.strides[i] = h->strides[i];
        desc_.sequence = seq;
        desc_.pts = pts;
    }
    ~UfdReceived() override {
        if (region_ && region_->hdr && region_->consumerIndex >= 0 &&
            slot_ < region_->hdr->numSlots) {
            __atomic_fetch_and(&region_->slotMeta(slot_)->holders,
                               ~(1ULL << region_->consumerIndex),
                               __ATOMIC_ACQ_REL);
        }
    }
    const halcodec::SharedFrameDesc& desc() const override { return desc_; }
    uint8_t* hostData() override { return region_->slotPixels(slot_); }
    size_t hostSize() override { return region_->hdr->slotBytes; }

private:
    std::shared_ptr<UfdRegion> region_;
    uint32_t slot_;
    halcodec::SharedFrameDesc desc_;
};

class UnixFdChannel : public FrameChannel {
public:
    UnixFdChannel(const ChannelConfig& cfg)
        : name_(cfg.name),
          producer_(cfg.producer),
          numSlots_(cfg.numSlots > 0 ? cfg.numSlots : 4),
          format_(cfg.format),
          width_(cfg.width),
          height_(cfg.height) {}

    ~UnixFdChannel() override { Close(); }

    bool Open() override {
        if (open_) return true;
        return producer_ ? OpenProducer() : OpenConsumer();
    }

    uint8_t* AcquireHostSlot(size_t bytes) override {
        if (!producer_ || !region_ || !region_->base) return nullptr;
        if (bytes > region_->hdr->slotBytes) {
            std::cerr << "unixfd: frame is " << bytes << " bytes but the ring slot "
                      << "is " << region_->hdr->slotBytes
                      << " (geometry from ChannelConfig is too small)\n";
            return nullptr;
        }
        for (uint32_t i = 0; i < region_->hdr->numSlots; ++i) {
            uint64_t expected = 0;
            // 空闲槽:位图为 0。CAS 成"生产者预留",避免重入与消费者误取。
            if (__atomic_compare_exchange_n(&region_->slotMeta(i)->holders,
                                            &expected, kReservedBit,
                                            false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE)) {
                reservedSlot_ = static_cast<int>(i);
                return region_->slotPixels(i);
            }
        }
        return nullptr;  // 全部槽被消费者占用 → 丢弃本帧
    }

    bool Publish(const halcodec::SharedFrameDesc& desc) override {
        if (!producer_ || !region_ || reservedSlot_ < 0) return false;
        SlotMeta* meta = region_->slotMeta(reservedSlot_);
        __atomic_store_n(&meta->seq, desc.sequence, __ATOMIC_RELEASE);

        Msg m{kMsgFrame, static_cast<uint32_t>(reservedSlot_), desc.sequence,
              desc.pts};

        std::lock_guard<std::mutex> lk(consMu_);  // 与 accept 线程的增删互斥
        // 本帧的初始持有者 = 此刻已连上的全部消费者。
        uint64_t mask = 0;
        for (auto& kv : consumers_) mask |= (1ULL << kv.first);
        __atomic_store_n(&meta->holders, mask, __ATOMIC_RELEASE);

        for (auto& kv : consumers_) {
            if (::send(kv.second, &m, sizeof(m), MSG_NOSIGNAL) !=
                static_cast<ssize_t>(sizeof(m))) {
                // 慢消费者:socket 满(或对端已走)→ 本帧不给它,清它的位。
                __atomic_fetch_and(&meta->holders, ~(1ULL << kv.first),
                                   __ATOMIC_ACQ_REL);
            }
        }
        reservedSlot_ = -1;
        return true;
    }

    std::unique_ptr<ReceivedFrame> Subscribe() override {
        if (producer_ || !region_ || region_->sock < 0) return nullptr;
        Msg m;
        while (true) {
            // Poll rather than block: the producer's control socket is
            // non-blocking and a full ring can drop its "stream ended" message,
            // so the end of the stream is read out of the shared header instead
            // (producerAlive) and only the pixels/frames come over the socket.
            pollfd pfd{region_->sock, POLLIN, 0};
            int pr = ::poll(&pfd, 1, 200);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return nullptr;
            }
            if (pr == 0) {
                if (__atomic_load_n(&region_->hdr->producerAlive, __ATOMIC_ACQUIRE) == 0) {
                    return nullptr;  // 流结束(may have dropped the close msg)
                }
                continue;
            }
            ssize_t n = ::recv(region_->sock, &m, sizeof(m), 0);
            if (n == 0) return nullptr;  // 生产者关闭
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                return nullptr;  // Close() 里的 shutdown,或对端消失
            }
            if (n != static_cast<ssize_t>(sizeof(m))) continue;
            if (m.type == kMsgClose) return nullptr;
            if (m.type != kMsgFrame) continue;
            return std::make_unique<UfdReceived>(region_, m.index, m.seq, m.pts);
        }
    }

    void Close() override {
        if (!open_) return;
        if (producer_) {
            if (region_ && region_->base) {
                __atomic_store_n(&region_->hdr->producerAlive, 0, __ATOMIC_RELEASE);
                Msg m{kMsgClose, 0, 0, 0};
                std::lock_guard<std::mutex> lk(consMu_);
                for (auto& kv : consumers_) {
                    ::send(kv.second, &m, sizeof(m), MSG_NOSIGNAL);
                    ::close(kv.second);
                }
                consumers_.clear();
            }
            // 唤醒并收 accept 线程。
            if (stopPipe_[1] >= 0) {
                char c = 1;
                ssize_t r = ::write(stopPipe_[1], &c, 1);
                (void)r;
            }
            if (acceptThread_.joinable()) acceptThread_.join();
            if (listenFd_ >= 0) {
                ::close(listenFd_);
                listenFd_ = -1;
            }
            for (int fd : stopPipe_) {
                if (fd >= 0) ::close(fd);
            }
            stopPipe_[0] = stopPipe_[1] = -1;
        } else if (region_ && region_->sock >= 0) {
            // 唤醒阻塞中的 Subscribe。
            ::shutdown(region_->sock, SHUT_RDWR);
        }
        region_.reset();
        open_ = false;
    }

    halcodec::FrameLocality locality() const override {
        return halcodec::FrameLocality::Host;
    }
    const std::string& name() const override { return name_; }

private:
    bool OpenProducer() {
        if (width_ <= 0 || height_ <= 0) {
            std::cerr << "unixfd: producer needs geometry in ChannelConfig "
                         "(width/height); the ring is sized before consumers "
                         "attach\n";
            return false;
        }
        slotBytes_ = SlotBytesFor(format_, width_, height_);
        const Layout lay = ComputeLayout(numSlots_, slotBytes_);

        int memfd = MakeMemfd(name_);
        if (memfd < 0) {
            std::cerr << "unixfd: memfd_create failed: " << std::strerror(errno)
                      << "\n";
            return false;
        }
        if (::ftruncate(memfd, static_cast<off_t>(lay.total)) != 0) {
            std::cerr << "unixfd: ftruncate failed: " << std::strerror(errno) << "\n";
            ::close(memfd);
            return false;
        }
        void* base = ::mmap(nullptr, lay.total, PROT_READ | PROT_WRITE,
                            MAP_SHARED, memfd, 0);
        if (base == MAP_FAILED) {
            std::cerr << "unixfd: mmap failed: " << std::strerror(errno) << "\n";
            ::close(memfd);
            return false;
        }
        std::memset(base, 0, lay.total);
        auto* hdr = static_cast<ShmHeader*>(base);
        hdr->magic = kMagic;
        hdr->version = kVersion;
        hdr->numSlots = static_cast<uint32_t>(numSlots_);
        hdr->slotBytes = slotBytes_;
        hdr->format = static_cast<uint32_t>(format_);
        hdr->width = static_cast<uint32_t>(width_);
        hdr->height = static_cast<uint32_t>(height_);
        hdr->producerAlive = 1;
        hdr->slotTableOffset = lay.slotTable;
        hdr->pixelOffset = lay.pixels;
        hdr->totalBytes = lay.total;

        int lfd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (lfd < 0) {
            std::cerr << "unixfd: socket failed: " << std::strerror(errno) << "\n";
            ::munmap(base, lay.total);
            ::close(memfd);
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const std::string path = SocketPath(name_);
        if (path.size() > sizeof(addr.sun_path)) {
            std::cerr << "unixfd: channel name too long\n";
            ::close(lfd);
            ::munmap(base, lay.total);
            ::close(memfd);
            return false;
        }
        std::memcpy(addr.sun_path, path.data(), path.size());
        socklen_t alen = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + path.size());
        if (::bind(lfd, reinterpret_cast<sockaddr*>(&addr), alen) != 0) {
            // 抽象域不落文件,失败通常是名字已被占用(同进程另一个生产者)。
            std::cerr << "unixfd: bind failed for '" << name_
                      << "': " << std::strerror(errno) << "\n";
            ::close(lfd);
            ::munmap(base, lay.total);
            ::close(memfd);
            return false;
        }
        if (::listen(lfd, 8) != 0) {
            std::cerr << "unixfd: listen failed: " << std::strerror(errno) << "\n";
            ::close(lfd);
            ::munmap(base, lay.total);
            ::close(memfd);
            return false;
        }

        region_ = std::make_shared<UfdRegion>();
        region_->base = base;
        region_->size = lay.total;
        region_->hdr = hdr;
        region_->producer = true;
        region_->sock = memfd;  // 留给 accept 线程传 fd;Close 时关

        listenFd_ = lfd;
        if (::pipe(stopPipe_) != 0) stopPipe_[0] = stopPipe_[1] = -1;
        acceptThread_ = std::thread([this] { AcceptLoop(); });
        open_ = true;
        return true;
    }

    bool OpenConsumer() {
        int sfd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (sfd < 0) {
            std::cerr << "unixfd: socket failed: " << std::strerror(errno) << "\n";
            return false;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const std::string path = SocketPath(name_);
        std::memcpy(addr.sun_path, path.data(), path.size());
        socklen_t alen = static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) + path.size());

        // 生产进程可能稍后才起来:短重试,而不是立刻失败。
        int rc = -1;
        for (int attempt = 0; attempt < 200; ++attempt) {
            rc = ::connect(sfd, reinterpret_cast<sockaddr*>(&addr), alen);
            if (rc == 0) break;
            if (errno != ENOENT && errno != ECONNREFUSED) break;
            ::usleep(10000);  // 10ms × 200 = 2s
        }
        if (rc != 0) {
            std::cerr << "unixfd: connect to '" << name_
                      << "' failed: " << std::strerror(errno) << "\n";
            ::close(sfd);
            return false;
        }

        // HELLO:字节 + SCM_RIGHTS(环 memfd)。
        Msg hello{};
        char cbuf[CMSG_SPACE(sizeof(int))] = {};
        iovec iov{&hello, sizeof(hello)};
        msghdr mh{};
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cbuf;
        mh.msg_controllen = sizeof(cbuf);
        ssize_t n = ::recvmsg(sfd, &mh, 0);
        if (n <= 0) {
            std::cerr << "unixfd: hello failed for '" << name_ << "'\n";
            ::close(sfd);
            return false;
        }
        int memfd = -1;
        for (cmsghdr* cmsg = CMSG_FIRSTHDR(&mh); cmsg; cmsg = CMSG_NXTHDR(&mh, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                std::memcpy(&memfd, CMSG_DATA(cmsg), sizeof(int));
            }
        }
        if (hello.type != kMsgHello || memfd < 0) {
            std::cerr << "unixfd: bad hello for '" << name_ << "' (n=" << n
                      << " type=" << hello.type << " idx=" << hello.index
                      << " memfd=" << memfd << " errno=" << errno << ")\n";
            if (memfd >= 0) ::close(memfd);
            ::close(sfd);
            return false;
        }

        struct stat st{};
        if (::fstat(memfd, &st) != 0 || st.st_size <= 0) {
            std::cerr << "unixfd: fstat on shared ring failed\n";
            ::close(memfd);
            ::close(sfd);
            return false;
        }
        size_t size = static_cast<size_t>(st.st_size);
        void* base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
        ::close(memfd);  // 映射已建立,fd 可关
        if (base == MAP_FAILED) {
            std::cerr << "unixfd: mmap failed: " << std::strerror(errno) << "\n";
            ::close(sfd);
            return false;
        }
        auto* hdr = static_cast<ShmHeader*>(base);
        if (hdr->magic != kMagic || hdr->version != kVersion) {
            std::cerr << "unixfd: ring header mismatch for '" << name_ << "'\n";
            ::munmap(base, size);
            ::close(sfd);
            return false;
        }

        region_ = std::make_shared<UfdRegion>();
        region_->base = base;
        region_->size = size;
        region_->hdr = hdr;
        region_->producer = false;
        region_->consumerIndex = static_cast<int>(hello.index);
        region_->sock = sfd;
        open_ = true;
        return true;
    }

    void AcceptLoop() {
        while (true) {
            std::vector<pollfd> pfds;
            pfds.push_back({listenFd_, POLLIN, 0});
            if (stopPipe_[0] >= 0) pfds.push_back({stopPipe_[0], POLLIN, 0});
            {
                std::lock_guard<std::mutex> lk(consMu_);
                for (auto& kv : consumers_) pfds.push_back({kv.second, 0, 0});
            }
            int r = ::poll(pfds.data(), pfds.size(), -1);
            if (r < 0) {
                if (errno == EINTR) continue;
                return;
            }
            if (pfds[0].revents & POLLIN) {
                // 先不设 SOCK_NONBLOCK:HELLO(含 SCM_RIGHTS)必须能立刻送出去,
                // 否则消费者 recvmsg 拿到的是半截握手。握手完成后单独置非阻塞
                // (见 HandshakeConsumer),之后每帧 send 才是可丢的。
                int cfd = ::accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC);
                if (cfd >= 0) HandshakeConsumer(cfd);
            }
            if (pfds.size() > 1 && (pfds[1].revents & POLLIN)) return;  // stop
            // 已注册消费者的 HUP/ERR:对端走了,补清它的持有位,否则槽会被永久占用。
            {
                std::lock_guard<std::mutex> lk(consMu_);
                size_t base = 2;
                for (size_t i = 0; i < consumers_.size(); ++i) {
                    if (base + i >= pfds.size()) break;
                    if (pfds[base + i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                        RetireConsumerLocked(pfds[base + i].fd);
                    }
                }
            }
        }
    }

    void HandshakeConsumer(int cfd) {
        // 位序号:最小空闲位。整个握手(登记 + 送 HELLO)都在 consMu_ 下完成:
        // 一旦消费者出现在 consumers_ 里,Publish 就可能往它 socket 上 push 帧,
        // 若那时 HELLO 还没发出去,消费者首个 recvmsg 收到的会是帧而不是握手
        // (环 memfd 也就没传过去)。新 socket 缓冲为空,24 字节不会阻塞。
        std::lock_guard<std::mutex> lk(consMu_);
        int index = -1;
        for (int i = 0; i < kMaxConsumers; ++i) {
            if (consumers_.find(i) == consumers_.end()) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            std::cerr << "unixfd: '" << name_ << "' consumer limit reached ("
                      << kMaxConsumers << ")\n";
            ::close(cfd);
            return;
        }

        // 先登记(消费者此刻已在 consumers_ 里,Publish 会在本锁保护的临界区外
        // 等锁);HELLO 仍在锁内送出,所以 Publish 不可能抢在握手之前 push 帧。
        consumers_[index] = cfd;

        Msg hello{kMsgHello, static_cast<uint32_t>(index), 0, 0};
        char cbuf[CMSG_SPACE(sizeof(int))] = {};
        iovec iov{&hello, sizeof(hello)};
        msghdr mh{};
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cbuf;
        mh.msg_controllen = sizeof(cbuf);
        cmsghdr* cmsg = CMSG_FIRSTHDR(&mh);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        int memfd = region_->sock;  // 生产者的环 memfd
        std::memcpy(CMSG_DATA(cmsg), &memfd, sizeof(int));
        if (::sendmsg(cfd, &mh, MSG_NOSIGNAL) < 0) {
            RetireConsumerLocked(cfd);
            return;
        }
        // 握手已送达。此后每帧 send 改为非阻塞:环满时丢弃该帧,而不是把生产者
        // 卡在一个慢消费者上(drop 路径见 Publish)。
        int flags = ::fcntl(cfd, F_GETFL, 0);
        if (flags >= 0) ::fcntl(cfd, F_SETFL, flags | O_NONBLOCK);
    }

    // 摘掉一个消费者并清掉它仍在位的持有位(异常退出路径)。调用方须持 consMu_。
    void RetireConsumerLocked(int cfd) {
        int index = -1;
        for (auto& kv : consumers_) {
            if (kv.second == cfd) {
                index = kv.first;
                break;
            }
        }
        if (index < 0) return;
        consumers_.erase(index);
        ::close(cfd);
        if (region_ && region_->base) {
            const uint64_t bit = 1ULL << index;
            for (uint32_t i = 0; i < region_->hdr->numSlots; ++i) {
                __atomic_fetch_and(&region_->slotMeta(i)->holders, ~bit,
                                   __ATOMIC_ACQ_REL);
            }
        }
    }

    std::string name_;
    bool producer_;
    int numSlots_;
    halcodec::PixelFormat format_;
    int width_, height_;
    uint64_t slotBytes_ = 0;
    std::shared_ptr<UfdRegion> region_;
    int reservedSlot_ = -1;
    bool open_ = false;

    int listenFd_ = -1;
    int stopPipe_[2] = {-1, -1};
    std::thread acceptThread_;
    std::mutex consMu_;
    std::map<int, int> consumers_;  // 位序号 -> socket
};

// 消费者取到的一帧定义在 UnixFdChannel 之前(Subscribe 要构造它)。

} // namespace

void RegisterUnixFdChannel() {
    RegisterFrameChannelFactory(
        "unixfd", [](const ChannelConfig& cfg) -> std::unique_ptr<FrameChannel> {
            return std::make_unique<UnixFdChannel>(cfg);
        });
}

namespace {
struct UnixFdRegistrar {
    UnixFdRegistrar() { RegisterUnixFdChannel(); }
} g_unixFdRegistrar;
} // namespace

} // namespace camera
