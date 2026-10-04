// Unit tests for the JPEG codestream front end: the marker walk that locates one
// image and reads its frame header. The crafted cases exist to pin the two traps a
// naive scan falls into -- an EXIF APP1 segment holding a whole thumbnail JPEG, and
// entropy-coded data whose stuffed 0xFF pairs can spell a marker -- and, given a
// real file as argv[1], the walk is checked against one too: a generator nobody has
// compared against an encoder is only as good as its author's guess.

#include "jpegdesc.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "test_check.h"

using namespace halcodec::vtbox;

namespace {

constexpr uint8_t kM = 0xFF;

void Push(std::vector<uint8_t>* v, const std::vector<uint8_t>& add) {
    v->insert(v->end(), add.begin(), add.end());
}

// A length-prefixed marker segment: FF <marker> <length:2 BE, counting itself>.
void AppendSegment(std::vector<uint8_t>* out, uint8_t marker,
                   const std::vector<uint8_t>& body) {
    const uint16_t len = static_cast<uint16_t>(body.size() + 2);
    out->push_back(kM);
    out->push_back(marker);
    out->push_back(static_cast<uint8_t>(len >> 8));
    out->push_back(static_cast<uint8_t>(len & 0xFF));
    Push(out, body);
}

// SOF body: precision, height, width, component count, then one 3-byte spec per
// component (identifier, sampling factors, quantisation table selector).
std::vector<uint8_t> FrameBody(int precision, int width, int height,
                               const std::vector<std::pair<int, int>>& sampling) {
    std::vector<uint8_t> body = {
        static_cast<uint8_t>(precision),
        static_cast<uint8_t>(height >> 8), static_cast<uint8_t>(height & 0xFF),
        static_cast<uint8_t>(width >> 8), static_cast<uint8_t>(width & 0xFF),
        static_cast<uint8_t>(sampling.size())};
    for (size_t i = 0; i < sampling.size(); ++i) {
        body.push_back(static_cast<uint8_t>(i + 1));
        body.push_back(static_cast<uint8_t>((sampling[i].first << 4) |
                                            sampling[i].second));
        body.push_back(0);
    }
    return body;
}

void AppendScanHeader(std::vector<uint8_t>* v) {
    AppendSegment(v, 0xDA, {0x03, 1, 0, 2, 0, 3, 0, 0, 39, 0});
}

std::vector<uint8_t> Baseline320x240() {
    std::vector<uint8_t> v = {kM, 0xD8};  // SOI
    AppendSegment(&v, 0xE0, {'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0});
    AppendSegment(&v, 0xDB, {0x00, 1, 2, 3});
    AppendSegment(&v, 0xC0, FrameBody(8, 320, 240, {{2, 2}, {1, 1}, {1, 1}}));
    AppendSegment(&v, 0xC4, {0x00, 1, 2, 3});
    AppendScanHeader(&v);
    // Entropy-coded data: a stuffed byte, a restart marker, then the EOC.
    Push(&v, {0x11, kM, 0x00, 0x22, kM, 0xD0, kM, 0x00, 0x33});
    Push(&v, {kM, 0xD9});
    return v;
}

void TestBaseline() {
    const std::vector<uint8_t> v = Baseline320x240();
    JpegCodestream s;
    CHECK(haltest::check(FindJpegCodestream(v.data(), v.size(), &s), __FILE__,
                         __LINE__, "baseline: parse must succeed"));
    haltest::check(s.complete, __FILE__, __LINE__,
                   "baseline: end of image must be found");
    haltest::check(s.start == 0, __FILE__, __LINE__, "baseline: SOI at 0");
    haltest::check(s.size == v.size(), __FILE__, __LINE__,
                   "baseline: the span covers the file");
    haltest::check(s.width == 320 && s.height == 240, __FILE__, __LINE__,
                   "baseline: 320x240 expected, got " +
                       std::to_string(s.width) + "x" +
                       std::to_string(s.height));
    haltest::check(s.precision == 8, __FILE__, __LINE__,
                   "baseline: 8-bit precision expected");
    haltest::check(s.componentCount == 3, __FILE__, __LINE__,
                   "baseline: three components expected");
    haltest::check(s.subsampled420, __FILE__, __LINE__,
                   "baseline: 2x2/1x1/1x1 is 4:2:0");
}

void TestExifThumbnailIsSkipped() {
    // The main image is 640x480; the thumbnail inside APP1 is 80x60 and ends with
    // its own EOC. Taking either of those from the thumbnail would be silent --
    // the decode would just be the wrong size, or stop after a few thousand bytes.
    std::vector<uint8_t> thumb = {kM, 0xD8};
    AppendSegment(&thumb, 0xC0, FrameBody(8, 80, 60, {{2, 2}, {1, 1}, {1, 1}}));
    AppendScanHeader(&thumb);
    Push(&thumb, {0x44, 0x55});
    Push(&thumb, {kM, 0xD9});

    std::vector<uint8_t> v = {kM, 0xD8};
    std::vector<uint8_t> app1 = {'E', 'x', 'i', 'f', 0, 0};
    Push(&app1, thumb);
    AppendSegment(&v, 0xE1, app1);
    AppendSegment(&v, 0xC0, FrameBody(8, 640, 480, {{2, 2}, {1, 1}, {1, 1}}));
    AppendScanHeader(&v);
    Push(&v, {0x66});
    Push(&v, {kM, 0xD9});

    JpegCodestream s;
    CHECK(haltest::check(FindJpegCodestream(v.data(), v.size(), &s), __FILE__,
                         __LINE__, "exif: parse must succeed"));
    haltest::check(s.width == 640 && s.height == 480, __FILE__, __LINE__,
                   "exif: the main image must size the stream, not the thumbnail (" +
                       std::to_string(s.width) + "x" +
                       std::to_string(s.height) + ")");
    haltest::check(s.size == v.size(), __FILE__, __LINE__,
                   "exif: the thumbnail's EOC must not end the image");
    haltest::check(s.complete, __FILE__, __LINE__, "exif: complete");
}

void TestTruncated() {
    std::vector<uint8_t> v = Baseline320x240();
    v.resize(v.size() - 6);  // drops the tail, EOC included

    JpegCodestream s;
    CHECK(haltest::check(FindJpegCodestream(v.data(), v.size(), &s), __FILE__,
                         __LINE__, "truncated: the header still sizes the picture"));
    haltest::check(!s.complete, __FILE__, __LINE__,
                   "truncated: must report a missing end of image");
    haltest::check(s.width == 320 && s.height == 240, __FILE__, __LINE__,
                   "truncated: dimensions survive");
    haltest::check(s.start + s.size == v.size(), __FILE__, __LINE__,
                   "truncated: the span has to reach the end of the buffer");
}

void TestNoFrameHeader() {
    // SOI then a segment that runs past the buffer: nothing here can open a
    // session, so the caller must not be handed a half-read stream.
    std::vector<uint8_t> v = {kM, 0xD8};
    AppendSegment(&v, 0xE0, std::vector<uint8_t>(400, 0x7F));
    v.resize(40);
    JpegCodestream s;
    haltest::check(!FindJpegCodestream(v.data(), v.size(), &s), __FILE__,
                   __LINE__, "no frame header: must be refused");
}

void TestNotAJpeg() {
    const std::vector<uint8_t> h264 = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x1F};
    JpegCodestream s;
    haltest::check(!FindJpegCodestream(h264.data(), h264.size(), &s), __FILE__,
                   __LINE__, "an Annex-B stream is not a codestream");
    haltest::check(!LooksLikeJpeg(h264.data(), h264.size()), __FILE__, __LINE__,
                   "an Annex-B stream does not start with SOI");
    const std::vector<uint8_t> v = Baseline320x240();
    haltest::check(LooksLikeJpeg(v.data(), v.size()), __FILE__, __LINE__,
                   "a JPEG does");
}

void TestOtherFrameHeaders() {
    // SOF2 (progressive) is a frame header too, so the size still has to come
    // out. The subsampling flag is what decides whether the decoded buffer is the
    // two-plane 4:2:0 frame the output path hands back.
    std::vector<uint8_t> progressive = {kM, 0xD8};
    AppendSegment(&progressive, 0xC2,
                  FrameBody(8, 1920, 1080, {{2, 2}, {1, 1}, {1, 1}}));
    AppendScanHeader(&progressive);
    Push(&progressive, {kM, 0xD9});
    JpegCodestream s;
    CHECK(haltest::check(
        FindJpegCodestream(progressive.data(), progressive.size(), &s), __FILE__,
        __LINE__, "SOF2: parse must succeed"));
    haltest::check(s.width == 1920 && s.height == 1080 && s.subsampled420,
                   __FILE__, __LINE__, "SOF2: 1920x1080 4:2:0 expected");

    std::vector<uint8_t> yuv444 = {kM, 0xD8};
    AppendSegment(&yuv444, 0xC0, FrameBody(8, 320, 240, {{1, 1}, {1, 1}, {1, 1}}));
    AppendScanHeader(&yuv444);
    Push(&yuv444, {kM, 0xD9});
    CHECK(haltest::check(FindJpegCodestream(yuv444.data(), yuv444.size(), &s),
                         __FILE__, __LINE__, "4:4:4: parse must succeed"));
    haltest::check(!s.subsampled420, __FILE__, __LINE__,
                   "4:4:4 must not read as 4:2:0");

    std::vector<uint8_t> gray = {kM, 0xD8};
    AppendSegment(&gray, 0xC0, FrameBody(8, 320, 240, {{1, 1}}));
    AppendSegment(&gray, 0xDA, {0x01, 1, 0, 0, 39, 0});
    Push(&gray, {kM, 0xD9});
    CHECK(haltest::check(FindJpegCodestream(gray.data(), gray.size(), &s),
                         __FILE__, __LINE__, "gray: parse must succeed"));
    haltest::check(s.componentCount == 1 && !s.subsampled420, __FILE__, __LINE__,
                   "gray: one component, not 4:2:0");
}

void TestRealFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        std::printf("jpegdesc_test: cannot open fixture %s\n", path);
        ++haltest::failures();
        return;
    }
    std::vector<uint8_t> v;
    uint8_t buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        Push(&v, std::vector<uint8_t>(buf, buf + n));
    }
    fclose(f);
    JpegCodestream s;
    CHECK(haltest::check(FindJpegCodestream(v.data(), v.size(), &s), __FILE__,
                         __LINE__, "fixture: parse must succeed"));
    haltest::check(s.complete && s.start == 0 && s.size == v.size(), __FILE__,
                   __LINE__, "fixture: the whole file is the codestream");
    haltest::check(s.precision == 8 && s.subsampled420, __FILE__, __LINE__,
                   "fixture: 8-bit 4:2:0 expected");
    std::printf("jpegdesc_test: fixture %dx%d\n", s.width, s.height);
}

}  // namespace

int main(int argc, char** argv) {
    TestBaseline();
    TestExifThumbnailIsSkipped();
    TestTruncated();
    TestNoFrameHeader();
    TestNotAJpeg();
    TestOtherFrameHeaders();
    if (argc > 1) {
        TestRealFile(argv[1]);
    }
    return haltest::finish("jpegdesc_test");
}
