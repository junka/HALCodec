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
#include "nvmedia_ijpd.h"
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

    // --- JPEG (IJPD) path ---
    // JPEG is image-level: there is no NvMediaParser for it, and the IDE video
    // decoder rejects MJPEG at NvMediaIDECreate. Decode goes through the
    // dedicated IJPD image-JPEG engine: each JPEG (delimited by SOI FF D8 ...
    // EOI FF D9) becomes one decoded NV12 frame. SetupJpegDecoder opens the
    // NvSci modules + EOF sync object; DecodeJpeg renders one JPEG bitstream
    // into a pooled PitchLinear YUV420 surface, reads it back via GetPixels,
    // and enqueues a CodecFrame.
    bool SetupJpegDecoder();
    bool DecodeJpeg(const uint8_t* data, size_t size);

    // Feed one fread buffer to the parser. Annex-B streams (h264/hevc/mpeg2)
    // go in as a single raw chunk; IVF-wrapped VP9/AV1 are demuxed here and
    // each frame is fed as a complete-picture packet. Returns false on parse
    // error.
    bool FeedFileChunk(const uint8_t* data, size_t size);

    // VC1 Simple/Main has no start codes and no in-band sequence header, so
    // NvMediaParser cannot recover frames from a raw byte feed and cannot
    // self-detect the sequence. An RCV container supplies both: the sequence
    // header is parsed out of the file header and handed to NvMediaParserCreate
    // via pExternalSeqInfo, and the per-frame length-prefixed records are fed
    // as discrete complete-picture packets. Mirrors videodemo.c
    // StreamVC1SimpleProfile + ParseRCVHeader. Returns false on a malformed
    // header; `consumed` is the header byte count to skip before frame records.
    bool ParseRcvHeader(const uint8_t* data, size_t size, size_t& consumed);

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
    // IJPD (image JPEG decoder) handle + output surface pool. Used only when
    // codec_ == NVMEDIA_VIDEO_CODEC_MJPEG; parser_/decoder_ stay null in that
    // case (JPEG has no NvMediaParser, and IDE rejects MJPEG).
    NvMediaIJPD* ijpd_ = nullptr;
    NvSciBufObj ijpdOutBuf_ = nullptr;   // reusable YUV420 output surface
    uint32_t ijpdMaxBits_ = 0;           // maxBitstreamBytes handed to IJPDCreate
    bool ijpdReady_ = false;             // IJPD + sync plumbing initialized

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
    // IVF container demuxer for file-mode VP9/AV1 input. NvMediaParser cannot
    // self-split a raw VP9/AV1 frame stream (no start codes), so an IVF input
    // is demuxed here and each frame is fed as its own packet with
    // bCompletePicture=1. Annex-B inputs (h264/hevc/mpeg2) keep the raw
    // chunk feed below. -1 = not sniffed yet, 0 = not IVF, >0 = header size.
    int ivfHeaderSize_ = -1;
    uint32_t ivfPayloadLeft_ = 0;       // bytes remaining in the current frame
    uint8_t ivfRecord_[12];             // 4-byte LE size + 8-byte pts
    uint32_t ivfRecordLen_ = 0;         // bytes filled into ivfRecord_
    bool feedCompletePicture_ = false;  // set bCompletePicture (vp9/av1)
    // VC1 SP/MP RCV container demuxer. -1 = not sniffed yet, 0 = not RCV,
    // >0 = file-header byte count (already skipped before frame records).
    int rcvHeaderSize_ = -1;
    uint32_t rcvFrameLeft_ = 0;       // payload bytes remaining in current frame
    bool rcvFrameHeaderRead_ = false; // 8-byte (size+pts) record parsed for cur frame
    uint8_t rcvRecord_[8];            // 4-byte LE size + 4-byte LE pts
    uint32_t rcvRecordLen_ = 0;       // bytes filled into rcvRecord_
    bool rcvSkippedFrame_ = false;    // frameSize==0 → feed a 1-byte 0x00
    // External SeqInfo built from the RCV header; kept alive for the parser's
    // lifetime because pExternalSeqInfo is a non-owning pointer read at create.
    NvMediaParserSeqInfo rcvSeqInfo_{};
    bool rcvExternalSeq_ = false;
    bool inputEof_ = false;    // fread consumed the whole file (or caller EOF)
    bool flushSent_ = false;   // EOS packet + NvMediaParserFlush sent
    bool streamDone_ = false;  // input exhausted AND parser flushed AND queue empty
    bool finalized_ = false;
    bool error_ = false;

    // JPEG (IJPD) accumulation state. The input file (a single .jpg or an
    // MJPEG motion stream of concatenated JPEGs) is read in full then split
    // into SOI(FF D8)..EOI(FF D9) segments; each segment is one decode call.
    std::vector<uint8_t> jpegFileBuffer_;
    std::deque<std::vector<uint8_t>> ijpdPending_;  // framed JPEGs awaiting decode

    // Decoded frames awaiting GetFrame().
    std::deque<CodecFrame> outQueue_;
};

} // namespace nvmedia
} // namespace halcodec

#endif // NVMEDIA_LAYERS_NVMEDIADECODER_H