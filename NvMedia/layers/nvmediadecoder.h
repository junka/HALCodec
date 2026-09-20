#ifndef NVMEDIA_LAYERS_NVMEDIADECODER_H
#define NVMEDIA_LAYERS_NVMEDIADECODER_H

#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "codec_config.h"
#include "decoder.h"
#include "frame.h"

#include "nvmedia_ide.h"
#include "nvmedia_parser.h"
#include "nvscibuf.h"
#include "nvscisync.h"

namespace halcodec {
namespace nvmedia {

// NvMediaIDE hardware decoder backend for NVIDIA DRIVE OS (Linux aarch64).
//
// Synchronous data path (isAsync() == false), driven exactly like the
// videodemo.c parser-callback model:
//   PullFrames() -> NvMediaParserParse() -> cbBeginSequence (lazily creates
//   the IDE decoder + surface pool) / cbDecodePicture (renders into a
//   registered NvSciBufObj) -> NvSciSyncFenceWait -> NvSciBufObjGetPixels
//   memcpy into a malloc'd CodecFrame -> outQueue_ (drained by GetFrame()).
//
// The IDE decoder cannot be created in Initialize() because NvMediaIDECreate
// needs the coded resolution, which is only known once the parser reports the
// sequence (cbBeginSequence).
class NvMediaDecoder : public halcodec::Decoder {
public:
    NvMediaDecoder() = default;
    ~NvMediaDecoder() override;

    bool Initialize(const CodecParams& params) override;
    int FillInput(const uint8_t* data, size_t size) override;
    bool SignalInputComplete() override;
    int PullFrames() override;
    void Finalize() override;
    bool GetFrame(CodecFrame& out) override;
    std::string getName() const override;

private:
    // One surface-slot of the decoded buffer pool. The parser hands these
    // slots out through cbAllocPictureBuffer as NvMediaRefSurface* (which is
    // a void*), so the pool entry pointer is cast to/from NvMediaRefSurface.
    struct Surface {
        NvSciBufObj buf = nullptr;  // NvSciBufObj registered with the decoder
        NvSciSyncFence fence{};     // EOF pre-fence for this surface
        int32_t refCount = 0;       // parser-managed reference count
        int32_t borrowed = 0;       // frames outstanding that still hold this buf
        uint32_t width = 0;         // coded (aligned) width
        uint32_t height = 0;        // coded (aligned) height
    };

    // Lazily create the IDE decoder + NvSciSync/NvSciBuf plumbing once the
    // parser reports the sequence. Mirrors videodemo.c cbBeginSequence.
    bool SetupDecoder(const NvMediaParserSeqInfo* seq);

    // Render + fence-wait + copy the decoded surface into a CodecFrame and
    // enqueue it. Mirrors videodemo.c cbDecodePicture.
    void OnDecodePicture(NvMediaParserPictureData* pd);

    // Copy a decoded surface into a freshly allocated CodecFrame via
    // NvSciBufObjGetPixels (block-linear -> linear is handled internally).
    CodecFrame CopySurfaceToFrame(Surface* s, NvMediaParserPictureData* pd);

    // Borrow the surface's NvSciBufObj into a device-memory CodecFrame (no
    // detile/copy): the frame carries the GPU surface handle and returns it
    // via `release` (decrementing Surface::borrowed). AllocPictureBufferCb
    // skips slots with borrowed > 0 so the decoder cannot overwrite a frame
    // still held by a consumer. Used when params.zeroCopy is set.
    CodecFrame BorrowSurfaceToFrame(Surface* s);

    // Pump the input file until at least one frame is queued or the stream is
    // fully exhausted (EOF read + NvMediaParserFlush). Returns frames queued.
    int PumpNext();

    bool SaveSurfaceAttrs(NvSciBufAttrList list, uint32_t w, uint32_t h);

    // Translate pool-slot pointers in the parser picture info into the
    // registered NvSciBufObj (and insert pre-fences) before handing it to
    // NvMediaIDEDecoderRender. Mirrors videodemo.c UpdateNvMediaSurface*.
    void UpdateReferenceFrames(NvMediaParserPictureData* pd);

    // Parser client callbacks (static thunks). The callback table lives in a
    // static member function so it can reference the private thunks above.
    static const NvMediaParserClientCb* GetParserCallbacks();
    static int32_t BeginSequenceCb(void* ctx, const NvMediaParserSeqInfo* seq);
    static NvMediaStatus DecodePictureCb(void* ctx, NvMediaParserPictureData* pd);
    static NvMediaStatus DisplayPictureCb(void* ctx, NvMediaRefSurface* s, int64_t pts);
    static void UnhandledNALUCb(void* ctx, const uint8_t* buf, int32_t size);
    static NvMediaStatus AllocPictureBufferCb(void* ctx, NvMediaRefSurface** p);
    static void ReleaseCb(void* ctx, NvMediaRefSurface* s);
    static void AddRefCb(void* ctx, NvMediaRefSurface* s);
    static NvMediaStatus GetBackwardUpdatesCb(void* ctx, NvMediaVP9BackwardUpdates* b);

    NvMediaParser* parser_ = nullptr;
    NvMediaIDE* decoder_ = nullptr;

    // NvSci modules/objects (created in SetupDecoder).
    NvSciBufModule bufModule_ = nullptr;
    NvSciSyncModule syncModule_ = nullptr;
    NvSciSyncCpuWaitContext cpuWaitContext_ = nullptr;
    NvSciSyncObj eofSyncObj_ = nullptr;

    NvMediaVideoCodec codec_ = static_cast<NvMediaVideoCodec>(-1);
    NvMediaDecoderInstanceId instanceId_ = NVMEDIA_DECODER_INSTANCE_0;

    // Ranges expected by the parser callbacks.
    std::vector<Surface> pool_;
    uint32_t decodeBuffers_ = 0;   // uDecodeBuffers from seq info
    uint32_t codedWidth_ = 0;      // aligned coded width  (16-byte aligned)
    uint32_t codedHeight_ = 0;     // aligned coded height
    uint32_t displayWidth_ = 0;    // display (crop) size
    uint32_t displayHeight_ = 0;
    bool zeroCopy_ = false;        // emit NvSciBufObj device frames instead of GetPixels

    // Input demux state.
    FILE* inputFile_ = nullptr;
    std::vector<uint8_t> chunk_;
    // True when Initialize() was called without an input path: the caller owns
    // the byte stream and pushes it through FillInput() (the NVDEC feed-mode
    // contract). No file is read and GetFrame() flushes the parser itself.
    bool feedMode_ = false;
    bool inputEof_ = false;    // fread consumed the whole file (or caller EOF)
    bool flushSent_ = false;   // EOS packet + NvMediaParserFlush sent
    bool streamDone_ = false;  // input exhausted AND parser flushed AND queue empty
    bool finalized_ = false;
    bool error_ = false;

    // Decoded frames awaiting GetFrame().
    std::deque<CodecFrame> outQueue_;
};

} // namespace nvmedia
} // namespace halcodec

#endif // NVMEDIA_LAYERS_NVMEDIADECODER_H