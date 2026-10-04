// Unit tests for the ProRes frame front end: the fixed-header read and the split
// of concatenated frames on the leading byte count, for the YUV ('icpf') and the
// RAW ('prrf') frame tags alike. The crafted cases pin the three ways a naive
// split goes wrong -- trusting a count that is shorter than the header it would
// have to describe, treating a frame whose tail has not arrived as decodable
// (VideoToolbox answers that with -12902 and no picture), and reading the picture
// size from anywhere but the frame header, which is the only place a ProRes
// codestream states it -- plus the one way a RAW frame goes wrong: read as YUV, it
// opens the wrong session and the picture comes back as the wrong kind of pixels.
// Given real files as argv[1] and argv[2] the walk is checked against encoder
// output and against camera output too.

#include "proresdesc.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "test_check.h"

using namespace halcodec::vtbox;

namespace {

// A frame header as the encoders write it: count, 'icpf', 0x0094, subsampling
// flag, producer tag, width, height, then payload padding so the count is true.
std::vector<uint8_t> MakeFrame(int width, int height, bool is444, size_t payload,
                               const char* producer = "apl0") {
    const size_t total = 20 + payload;
    std::vector<uint8_t> v(total, 0xA5);
    v[0] = static_cast<uint8_t>(total >> 24);
    v[1] = static_cast<uint8_t>(total >> 16);
    v[2] = static_cast<uint8_t>(total >> 8);
    v[3] = static_cast<uint8_t>(total & 0xFF);
    std::memcpy(v.data() + 4, "icpf", 4);
    v[8] = 0x00;
    v[9] = 0x94;
    v[10] = 0x00;
    v[11] = is444 ? 0x01 : 0x00;
    std::memcpy(v.data() + 12, producer, 4);
    v[16] = static_cast<uint8_t>(width >> 8);
    v[17] = static_cast<uint8_t>(width & 0xFF);
    v[18] = static_cast<uint8_t>(height >> 8);
    v[19] = static_cast<uint8_t>(height & 0xFF);
    return v;
}

void Patch16(std::vector<uint8_t>* v, size_t off, uint16_t value) {
    (*v)[off] = static_cast<uint8_t>(value >> 8);
    (*v)[off + 1] = static_cast<uint8_t>(value & 0xFF);
}

// A ProRes RAW frame header as Apple's cameras write it: count, 'prrf', then the
// same slots the YUV flavours use -- 0x0088 where a 'icpf' frame has 0x0094, a
// subsampling slot that reads 0, the producer tag 'appl', and width and height as
// two big-endian 16-bits at 16 and 18. Measured against real material, on which
// the four bytes after the dimensions are 08 08 08 08 and the metadata behind them
// is little-endian floats; none of that is read here, because none of it decides
// where the next picture starts.
std::vector<uint8_t> MakeRawFrame(int width, int height, size_t payload) {
    const size_t total = 24 + payload;
    std::vector<uint8_t> v(total, 0x5A);
    v[0] = static_cast<uint8_t>(total >> 24);
    v[1] = static_cast<uint8_t>(total >> 16);
    v[2] = static_cast<uint8_t>(total >> 8);
    v[3] = static_cast<uint8_t>(total & 0xFF);
    std::memcpy(v.data() + 4, "prrf", 4);
    v[8] = 0x00;
    v[9] = 0x88;
    v[10] = 0x00;
    v[11] = 0x00;
    std::memcpy(v.data() + 12, "appl", 4);
    v[16] = static_cast<uint8_t>(width >> 8);
    v[17] = static_cast<uint8_t>(width & 0xFF);
    v[18] = static_cast<uint8_t>(height >> 8);
    v[19] = static_cast<uint8_t>(height & 0xFF);
    v[20] = 0x08;
    v[21] = 0x08;
    v[22] = 0x08;
    v[23] = 0x08;
    return v;
}

void TestSingleFrame() {
    const std::vector<uint8_t> v = MakeFrame(320, 240, false, 1000);
    ProresFrame f;
    CHECK(haltest::check(ReadProresFrame(v.data(), v.size(), &f), __FILE__,
                         __LINE__, "frame: parse must succeed"));
    haltest::check(f.size == v.size(), __FILE__, __LINE__,
                   "frame: count has to cover the whole frame, got " +
                       std::to_string(f.size));
    haltest::check(f.complete, __FILE__, __LINE__,
                   "frame: all bytes are here, so it must read as complete");
    haltest::check(f.width == 320 && f.height == 240, __FILE__, __LINE__,
                   "frame: 320x240 expected, got " + std::to_string(f.width) +
                       "x" + std::to_string(f.height));
    haltest::check(!f.is444, __FILE__, __LINE__, "frame: 4:2:2 expected");
    haltest::check(LooksLikeProRes(v.data(), v.size()), __FILE__, __LINE__,
                   "frame: sniff must accept it");
}

void TestChromaFlag() {
    // The flag decides which pixel format the decoded buffer comes back in, and
    // only the 4:2:2 one maps onto a HAL format, so both values matter.
    const std::vector<uint8_t> v = MakeFrame(320, 240, true, 64, "Lavc");
    ProresFrame f;
    CHECK(haltest::check(ReadProresFrame(v.data(), v.size(), &f), __FILE__,
                         __LINE__, "444: parse must succeed"));
    haltest::check(f.is444, __FILE__, __LINE__,
                   "444: subsampling flag must read as 4:4:4");
    // Producer tags differ between encoders ('apl0', 'Lavc') and are not a
    // subsampling signal; the second frame above proves a non-Apple tag parses.
}

void TestRawFrame() {
    const std::vector<uint8_t> v = MakeRawFrame(4112, 2176, 4096);
    ProresFrame f;
    CHECK(haltest::check(ReadProresFrame(v.data(), v.size(), &f), __FILE__,
                         __LINE__, "raw: parse must succeed"));
    haltest::check(f.isRaw, __FILE__, __LINE__,
                   "raw: a 'prrf' frame has to be flagged as RAW, or the decoder "
                   "opens a YUV session on it");
    haltest::check(!f.is444, __FILE__, __LINE__,
                   "raw: the subsampling slot is not a 4:4:4 signal");
    haltest::checkEq(f.size, v.size(), __FILE__, __LINE__,
                     "raw: count has to cover the whole frame");
    haltest::check(f.complete, __FILE__, __LINE__,
                   "raw: all bytes are here");
    haltest::check(f.width == 4112 && f.height == 2176, __FILE__, __LINE__,
                   "raw: 4112x2176 expected, got " + std::to_string(f.width) +
                       "x" + std::to_string(f.height));
    haltest::check(LooksLikeProRes(v.data(), v.size()), __FILE__, __LINE__,
                   "raw: the sniff has to accept it, or the stream is never "
                   "recognised as ProRes at all");
}

void TestRawIgnoresTheConstantField() {
    // The 0x0094 fingerprint is a property of the 'icpf' header. A RAW frame says
    // 0x0088 in that slot on every frame measured, but only one stream has been
    // available to measure, so a value there cannot be grounds for refusing real
    // material -- while the count and the dimensions still are.
    std::vector<uint8_t> v = MakeRawFrame(1920, 1080, 64);
    Patch16(&v, 8, 0x0099);
    ProresFrame f;
    haltest::check(ReadProresFrame(v.data(), v.size(), &f), __FILE__, __LINE__,
                   "raw: a different constant must not lose the frame");
    haltest::check(f.isRaw, __FILE__, __LINE__, "raw: still RAW");

    // The same buffer under the YUV tag is refused for the same field, which is
    // what keeps this a flavour rule rather than no rule at all.
    std::memcpy(v.data() + 4, "icpf", 4);
    haltest::check(!ReadProresFrame(v.data(), v.size(), &f), __FILE__, __LINE__,
                   "icpf: the constant field is still checked");

    // A RAW frame whose count is a lie is refused the way any frame is.
    std::vector<uint8_t> tiny = MakeRawFrame(1920, 1080, 64);
    tiny[3] = 0x0F;
    haltest::check(!LooksLikeProRes(tiny.data(), tiny.size()), __FILE__,
                   __LINE__, "raw: a count shorter than the header is refused");
    // ... and a zero-sized picture cannot open a session.
    std::vector<uint8_t> nodims = MakeRawFrame(1920, 1080, 64);
    Patch16(&nodims, 18, 0);
    haltest::check(!ReadProresFrame(nodims.data(), nodims.size(), &f), __FILE__,
                   __LINE__, "raw: zero height must be refused");
}

void TestMixedStream() {
    // The two frame tags interleaved, which is what a decoder's pump sees: the
    // split is on the counts alone, so a RAW picture in the middle must not throw
    // the walk off, and each frame must still carry its own flavour.
    const std::vector<uint8_t> a = MakeFrame(320, 240, false, 900);
    const std::vector<uint8_t> b = MakeRawFrame(320, 240, 1500);
    const std::vector<uint8_t> c = MakeFrame(320, 240, true, 2000);
    std::vector<uint8_t> stream;
    for (const std::vector<uint8_t>* f : {&a, &b, &c}) {
        stream.insert(stream.end(), f->begin(), f->end());
    }

    const bool expectRaw[3] = {false, true, false};
    const bool expect444[3] = {false, false, true};
    size_t pos = 0;
    int seen = 0;
    while (pos < stream.size()) {
        ProresFrame f;
        CHECK(haltest::check(
            ReadProresFrame(stream.data() + pos, stream.size() - pos, &f),
            __FILE__, __LINE__, "mixed: frame must parse"));
        haltest::check(f.complete, __FILE__, __LINE__,
                       "mixed: every frame is whole in this buffer");
        haltest::check(f.isRaw == expectRaw[seen], __FILE__, __LINE__,
                       "mixed: frame " + std::to_string(seen) + " flavour");
        haltest::check(f.is444 == expect444[seen], __FILE__, __LINE__,
                       "mixed: frame " + std::to_string(seen) + " subsampling");
        pos += f.size;
        ++seen;
    }
    haltest::checkEq(seen, 3, __FILE__, __LINE__, "mixed: frame count");
    haltest::checkEq(pos, stream.size(), __FILE__, __LINE__,
                     "mixed: counts must tile the buffer exactly");
}

void TestSplitStream() {
    // Three frames back to back, the layout hal_dec is handed for ProRes: the
    // split has to land on each boundary and stop exactly at the end.
    std::vector<uint8_t> stream;
    std::vector<size_t> sizes;
    for (size_t i = 0; i < 3; ++i) {
        const std::vector<uint8_t> f =
            MakeFrame(320, 240, false, 500 + i * 100);
        sizes.push_back(f.size());
        stream.insert(stream.end(), f.begin(), f.end());
    }

    size_t pos = 0;
    int frames = 0;
    while (pos < stream.size()) {
        ProresFrame f;
        CHECK(haltest::check(ReadProresFrame(stream.data() + pos,
                                             stream.size() - pos, &f),
                             __FILE__, __LINE__, "split: frame must parse"));
        haltest::check(f.complete, __FILE__, __LINE__,
                       "split: every frame is whole in this buffer");
        haltest::checkEq(f.size, sizes[frames], __FILE__, __LINE__,
                         "split: frame byte count");
        pos += f.size;
        ++frames;
    }
    haltest::checkEq(frames, 3, __FILE__, __LINE__, "split: frame count");
    haltest::checkEq(pos, stream.size(), __FILE__, __LINE__,
                     "split: counts must tile the buffer exactly");
}

void TestTruncated() {
    // The last bytes have not arrived. The frame is still describable (its header
    // is here), but submitting it decodes nothing, so `complete` has to be false
    // while `size` keeps reporting the full count the pump is waiting for.
    std::vector<uint8_t> v = MakeFrame(1920, 1080, false, 100000);
    v.resize(v.size() - 1);

    ProresFrame f;
    CHECK(haltest::check(ReadProresFrame(v.data(), v.size(), &f), __FILE__,
                         __LINE__, "truncated: the header still sizes the frame"));
    haltest::check(!f.complete, __FILE__, __LINE__,
                   "truncated: must report the frame as unfinished");
    haltest::checkEq(f.size, 100020u, __FILE__, __LINE__,
                     "truncated: the full count must survive");
    haltest::check(f.width == 1920 && f.height == 1080, __FILE__, __LINE__,
                   "truncated: dimensions survive");

    // Not even the header has arrived: nothing is known about this frame.
    const std::vector<uint8_t> thin = MakeFrame(320, 240, false, 1000);
    std::vector<uint8_t> head(thin.begin(), thin.begin() + 19);
    ProresFrame g;
    haltest::check(!ReadProresFrame(head.data(), head.size(), &g), __FILE__,
                   __LINE__, "short header: must be refused");
    // The sniff only needs the count and the tag, so a caller can tell ProRes
    // from another stream out of the first 8 bytes; the full header is what
    // turns that into a frame.
    haltest::check(LooksLikeProRes(head.data(), head.size()), __FILE__,
                   __LINE__, "short header: sniff still identifies the stream");
}

void TestRefusals() {
    ProresFrame f;

    // A count shorter than the header it would describe: cannot be a count.
    std::vector<uint8_t> tiny = MakeFrame(320, 240, false, 1000);
    Patch16(&tiny, 2, 0x0000);
    tiny[3] = 0x0F;  // 15 bytes, header is 20
    haltest::check(!LooksLikeProRes(tiny.data(), tiny.size()), __FILE__,
                   __LINE__, "short count: sniff must refuse");
    haltest::check(!ReadProresFrame(tiny.data(), tiny.size(), &f), __FILE__,
                   __LINE__, "short count: parse must refuse");

    // Right length, wrong frame tag.
    std::vector<uint8_t> notag = MakeFrame(320, 240, false, 1000);
    std::memcpy(notag.data() + 4, "ipcf", 4);
    haltest::check(!ReadProresFrame(notag.data(), notag.size(), &f), __FILE__,
                   __LINE__, "wrong tag: must be refused");

    // The 0x0094 field is what both encoders write; a buffer that only looks like
    // it otherwise is refused rather than handed to a session.
    std::vector<uint8_t> wrongconst = MakeFrame(320, 240, false, 1000);
    Patch16(&wrongconst, 8, 0x0095);
    haltest::check(LooksLikeProRes(wrongconst.data(), wrongconst.size()),
                   __FILE__, __LINE__,
                   "wrong constant: the sniff is deliberately cheap");
    haltest::check(!ReadProresFrame(wrongconst.data(), wrongconst.size(), &f),
                   __FILE__, __LINE__, "wrong constant: parse must refuse");

    // A picture of no size cannot open a session.
    std::vector<uint8_t> nodims = MakeFrame(320, 240, false, 1000);
    Patch16(&nodims, 16, 0);
    haltest::check(!ReadProresFrame(nodims.data(), nodims.size(), &f), __FILE__,
                   __LINE__, "zero width: must be refused");

    // Other things a caller may hand over by mistake.
    const std::vector<uint8_t> annexb = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42,
                                         0x1F, 0x00, 0x00, 0x00, 0x01, 0x68};
    haltest::check(!LooksLikeProRes(annexb.data(), annexb.size()), __FILE__,
                   __LINE__, "an Annex-B stream is not ProRes");
    haltest::check(!ReadProresFrame(annexb.data(), annexb.size(), &f), __FILE__,
                   __LINE__, "an Annex-B stream must not parse");
    const std::vector<uint8_t> jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A,
                                       0x46, 0x49, 0x46};
    haltest::check(!LooksLikeProRes(jpeg.data(), jpeg.size()), __FILE__,
                   __LINE__, "a JPEG is not ProRes");
    haltest::check(!ReadProresFrame(nullptr, 100, &f), __FILE__, __LINE__,
                   "null buffer: must be refused");
}

void TestRealFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        std::printf("proresdesc_test: cannot open fixture %s\n", path);
        ++haltest::failures();
        return;
    }
    std::vector<uint8_t> v;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        v.insert(v.end(), buf, buf + n);
    }
    fclose(f);

    haltest::check(LooksLikeProRes(v.data(), v.size()), __FILE__, __LINE__,
                   "fixture: must sniff as ProRes");
    size_t pos = 0;
    int frames = 0;
    while (pos < v.size()) {
        ProresFrame frame;
        CHECK(haltest::check(
            ReadProresFrame(v.data() + pos, v.size() - pos, &frame), __FILE__,
            __LINE__, "fixture: frame must parse"));
        haltest::check(frame.complete, __FILE__, __LINE__,
                       "fixture: every frame in the file is whole");
        haltest::check(frame.width == 320 && frame.height == 240, __FILE__,
                       __LINE__, "fixture: 320x240 expected, got " +
                                     std::to_string(frame.width) + "x" +
                                     std::to_string(frame.height));
        haltest::check(!frame.is444, __FILE__, __LINE__,
                       "fixture: this one is 4:2:2");
        pos += frame.size;
        ++frames;
    }
    haltest::checkEq(frames, 3, __FILE__, __LINE__, "fixture: frame count");
    std::printf("proresdesc_test: fixture %d frames, %zu bytes\n", frames,
                v.size());
}

void TestRawFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        std::printf("proresdesc_test: cannot open RAW fixture %s\n", path);
        ++haltest::failures();
        return;
    }
    std::vector<uint8_t> v;
    uint8_t buf[65536];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        v.insert(v.end(), buf, buf + n);
    }
    fclose(f);

    haltest::check(LooksLikeProRes(v.data(), v.size()), __FILE__, __LINE__,
                   "RAW fixture: must sniff as ProRes");
    size_t pos = 0;
    int frames = 0;
    int width = 0;
    int height = 0;
    while (pos < v.size()) {
        ProresFrame frame;
        CHECK(haltest::check(
            ReadProresFrame(v.data() + pos, v.size() - pos, &frame), __FILE__,
            __LINE__, "RAW fixture: frame must parse"));
        if (!frame.complete) {
            // The stream ends inside a picture, which the feed path has to
            // survive by handing back what it holds.
            std::printf("proresdesc_test: RAW fixture ends %zu bytes into a "
                        "%zu-byte picture\n",
                        v.size() - pos, frame.size);
            break;
        }
        haltest::check(frame.isRaw, __FILE__, __LINE__,
                       "RAW fixture: frame is not flagged RAW");
        haltest::check(!frame.is444, __FILE__, __LINE__,
                       "RAW fixture: a RAW frame is not 4:4:4 YUV");
        if (frames == 0) {
            width = frame.width;
            height = frame.height;
        } else {
            haltest::check(frame.width == width && frame.height == height,
                           __FILE__, __LINE__,
                           "RAW fixture: picture size changed mid-stream");
        }
        pos += frame.size;
        ++frames;
    }
    haltest::check(frames > 0, __FILE__, __LINE__,
                   "RAW fixture: holds no complete picture");
    // A RAW frame's count is the container's own sample size, so the walk has to
    // land on every boundary and stop at the end of the file.
    haltest::checkEq(pos, v.size(), __FILE__, __LINE__,
                     "RAW fixture: counts must tile the file exactly");
    std::printf("proresdesc_test: RAW fixture %d frames, %dx%d, %zu bytes\n",
                frames, width, height, v.size());
}

}  // namespace

int main(int argc, char** argv) {
    TestSingleFrame();
    TestChromaFlag();
    TestRawFrame();
    TestRawIgnoresTheConstantField();
    TestMixedStream();
    TestSplitStream();
    TestTruncated();
    TestRefusals();
    if (argc > 1) {
        TestRealFile(argv[1]);
    }
    if (argc > 2) {
        TestRawFile(argv[2]);
    }
    return haltest::finish("proresdesc_test");
}
