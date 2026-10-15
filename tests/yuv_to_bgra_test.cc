// yuv_to_bgra_test - hal_player's frame-to-BGRA conversion, with no decoder, no
// GPU and no window: hand-built CodecFrames in, BGRA bytes out.
//
// The conversion is the one place a decoder's output meets a picture, so the
// checks are of two kinds:
//
//   1. Absolutes. A frame with known samples must come out as the BT.601
//      numbers, to the integer approximation. This pins the arithmetic,
//      including the part that broke last time: chroma is unsigned with neutral
//      128, so 128 must read as 0 and 130 as +2. Read as a signed byte those are
//      -128 and -126, which saturate every pixel into a colour block -- the
//      exact regression this file is here to catch, so the sample values are
//      chosen to make that misreading land on different pixels than the right one.
//
//   2. Equivalences. Each format is a re-layout of the same samples, so a
//      frame derived from a reference NV12 frame must convert to the *same*
//      bytes: I420 is NV12's chroma de-interleaved into two planes, NV16/P210
//      duplicate each chroma row, P010/P016 left-shift each sample into the top
//      of its 16-bit word. Byte equality against the NV12 output tests the layout
//      dispatch (plane offsets, sub-sampling, per-plane stride) far more sharply
//      than a tolerance on a natural image would, and it is what makes a format
//      that is dispatched but mis-addressed fail loudly.
//
// Plus the two contract edges hal_player depends on: strides of 0 (CodecFrame's
// "tightly packed" sentinel) must equal the tight strides, and a format with no
// conversion must be refused rather than rendered as garbage.

#include <cstdint>
#include <cstring>
#include <vector>

#include "frame.h"
#include "test_check.h"
#include "yuv_to_bgra.h"

namespace {

using halcodec::CodecFrame;
using halcodec::PixelFormat;

constexpr int kW = 8;
constexpr int kH = 8;

// One BGRA pixel, as the converter packs it.
struct Rgb { int r, g, b; };

Rgb PixelAt(const std::vector<uint8_t>& bgra, int w, int row, int col) {
    const size_t o = (static_cast<size_t>(row) * w + col) * 4;
    return Rgb{bgra[o + 2], bgra[o + 1], bgra[o + 0]};
}

bool AlphaSet(const std::vector<uint8_t>& bgra) {
    for (size_t o = 3; o < bgra.size(); o += 4)
        if (bgra[o] != 0xFF) return false;
    return true;
}

// The BT.601 limited-range integers the converter is expected to produce, so the
// expectation is written once and a change to the constants has to be a change
// to this too.
Rgb Bt601(int y, int u, int v) {
    int R = y + (141 * (v - 128)) / 100;
    int G = y - (34 * (u - 128)) / 100 - (71 * (v - 128)) / 100;
    int B = y + (177 * (u - 128)) / 100;
    auto clamp = [](int c) { return c < 0 ? 0 : (c > 255 ? 255 : c); };
    return Rgb{clamp(R), clamp(G), clamp(B)};
}

// A synthetic NV12 frame. The chroma pairs given are tiled across the plane --
// one pair means flat chroma, a full row-major list means a gradient -- and no
// pair of the plane is left unwritten (a zeroed chroma byte is a maximal negative
// offset, which is a tint nobody asked for and would be read as a failure).
struct Nv12 {
    std::vector<uint8_t> bytes;
    CodecFrame frame;
};

Nv12 MakeNv12(int w, int h, const std::vector<uint8_t>& luma,
              const std::vector<std::pair<uint8_t, uint8_t>>& chroma) {
    Nv12 n;
    n.bytes.assign(static_cast<size_t>(w) * h * 3 / 2, 0);
    std::memcpy(n.bytes.data(), luma.data(), static_cast<size_t>(w) * h);
    uint8_t* uv = n.bytes.data() + static_cast<size_t>(w) * h;
    const int pairs = (w / 2) * (h / 2);
    for (int i = 0; i < pairs; i++) {
        const std::pair<uint8_t, uint8_t>& c = chroma[i % chroma.size()];
        uv[i * 2] = c.first;
        uv[i * 2 + 1] = c.second;
    }
    n.frame.data = n.bytes.data();
    n.frame.width = w;
    n.frame.height = h;
    n.frame.format = PixelFormat::NV12;
    return n;
}

std::vector<uint8_t> Convert(const CodecFrame& f, bool* ok = nullptr) {
    std::vector<uint8_t> out(static_cast<size_t>(f.width) * f.height * 4, 0);
    const bool r = halplayer::ConvertFrame(f, out.data());
    if (ok) *ok = r;
    return out;
}

// --- 1. Absolutes ---------------------------------------------------------

void TestNeutralChromaIsLuma() {
    // U=V=128 is chroma neutral, so the picture is grey at the luma value. Signed
    // reading of the raw byte makes 128 into -128 and this becomes saturated.
    std::vector<uint8_t> luma(kW * kH, 100);
    Nv12 n = MakeNv12(kW, kH, luma, {std::make_pair<uint8_t, uint8_t>(128, 128)});
    const std::vector<uint8_t> out = Convert(n.frame);
    const Rgb p = PixelAt(out, kW, 4, 4);
    CHECK_EQ(p.r, 100);
    CHECK_EQ(p.g, 100);
    CHECK_EQ(p.b, 100);
    CHECK(AlphaSet(out));
}

void TestChromaIsUnsigned() {
    // U=130 (+2) and V=126 (-2): the signed misreading gives -126 and -2. Small
    // offsets, so the right answer is a slight tint and the wrong one is a hard
    // swing -- the two cannot be confused for each other.
    std::vector<uint8_t> luma(kW * kH, 128);
    Nv12 n = MakeNv12(kW, kH, luma, {std::make_pair<uint8_t, uint8_t>(130, 126)});
    const Rgb p = PixelAt(Convert(n.frame), kW, 4, 4);
    const Rgb want = Bt601(128, 130, 126);
    CHECK_EQ(p.r, want.r);
    CHECK_EQ(p.g, want.g);
    CHECK_EQ(p.b, want.b);
    // And the values are in the small-tint neighbourhood, not the saturated one.
    CHECK(p.g > 120);
    CHECK(p.g < 136);
}

void TestLumaSpanAndClamp() {
    // Black (Y=16) and white (Y=235) at neutral chroma are the ends of the video
    // range: they must map onto 16 and 235, not be expanded to 0 and 255 (the
    // converter does no range expansion), and nothing may overflow past the byte.
    std::vector<uint8_t> luma(kW * kH, 16);
    for (int row = 0; row < 2; row++)
        for (int col = 0; col < kW; col++) luma[row * kW + col] = 235;
    Nv12 n = MakeNv12(kW, kH, luma, {std::make_pair<uint8_t, uint8_t>(128, 128)});
    const std::vector<uint8_t> out = Convert(n.frame);
    const Rgb white = PixelAt(out, kW, 0, 0);
    const Rgb black = PixelAt(out, kW, 4, 4);
    CHECK_EQ(white.r, 235);
    CHECK_EQ(black.r, 16);

    // Extreme chroma must clamp rather than wrap (255 + 1.772*127 overflows a byte).
    Nv12 hot = MakeNv12(kW, kH, std::vector<uint8_t>(kW * kH, 255),
                        {std::make_pair<uint8_t, uint8_t>(255, 255)});
    const Rgb p = PixelAt(Convert(hot.frame), kW, 4, 4);
    const Rgb want = Bt601(255, 255, 255);
    CHECK_EQ(p.r, want.r);
    CHECK_EQ(p.g, want.g);
    CHECK_EQ(p.b, want.b);
    CHECK_EQ(p.r, 255);
}

// --- 2. Equivalences against the NV12 reference ----------------------------

// A reference frame with real structure: a luma gradient and a two-axis chroma
// gradient, so a mis-addressed plane or a wrong sub-sampling step shows up.
struct Reference {
    int w, h;
    std::vector<uint8_t> luma;
    std::vector<std::pair<uint8_t, uint8_t>> chroma;
    Nv12 nv12;
    std::vector<uint8_t> bgra;
};

Reference MakeReference(int w, int h) {
    Reference r;
    r.w = w; r.h = h;
    r.luma.resize(static_cast<size_t>(w) * h);
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            r.luma[row * w + col] = static_cast<uint8_t>(20 + (7 * col + 5 * row) % 200);
    for (int i = 0; i < (w / 2) * (h / 2); i++) {
        const int c = i % (w / 2), rr = i / (w / 2);
        r.chroma.push_back(std::make_pair(static_cast<uint8_t>(32 + 6 * c),
                                          static_cast<uint8_t>(224 - 6 * rr)));
    }
    r.nv12 = MakeNv12(w, h, r.luma, r.chroma);
    bool ok = false;
    r.bgra = Convert(r.nv12.frame, &ok);
    CHECK(ok);
    return r;
}

// Compare a converted frame to the reference: every byte, so a wrong plane
// offset or sub-sampling step cannot hide behind an average.
void CheckSame(const std::vector<uint8_t>& got, const std::vector<uint8_t>& want,
               const char* what) {
    bool same = got.size() == want.size();
    if (same) {
        for (size_t i = 0; i < got.size(); i++) {
            if (got[i] != want[i]) { same = false; break; }
        }
    }
    if (!same) {
        // Locate the first difference so a failure names a pixel, not a buffer.
        size_t first = 0;
        while (first < got.size() && first < want.size() &&
               got[first] == want[first]) first++;
        std::printf("  %s: first differing byte at %zu (pixel %zu, channel %zu)\n",
                    what, first, first / 4, first % 4);
    }
    CHECK(same);
}

void TestI420MatchesNv12() {
    Reference r = MakeReference(16, 16);
    // De-interleave the NV12 chroma into Cb and Cr planes.
    std::vector<uint8_t> i420(static_cast<size_t>(r.w) * r.h * 3 / 2, 0);
    std::memcpy(i420.data(), r.luma.data(), static_cast<size_t>(r.w) * r.h);
    uint8_t* cb = i420.data() + static_cast<size_t>(r.w) * r.h;
    uint8_t* cr = cb + static_cast<size_t>(r.w / 2) * (r.h / 2);
    for (int i = 0; i < (r.w / 2) * (r.h / 2); i++) {
        cb[i] = r.chroma[i].first;
        cr[i] = r.chroma[i].second;
    }
    CodecFrame f;
    f.data = i420.data(); f.width = r.w; f.height = r.h;
    f.format = PixelFormat::I420;
    CheckSame(Convert(f), r.bgra, "I420");
}

void TestNv16MatchesNv12() {
    Reference r = MakeReference(16, 16);
    // 4:2:2: every chroma row is present, not one per two luma rows.
    std::vector<uint8_t> nv16(static_cast<size_t>(r.w) * r.h * 2, 0);
    std::memcpy(nv16.data(), r.luma.data(), static_cast<size_t>(r.w) * r.h);
    const uint8_t* uv = r.nv12.bytes.data() + static_cast<size_t>(r.w) * r.h;
    uint8_t* out = nv16.data() + static_cast<size_t>(r.w) * r.h;
    for (int row = 0; row < r.h; row++)
        std::memcpy(out + static_cast<size_t>(row) * r.w,
                    uv + static_cast<size_t>(row / 2) * r.w, r.w);
    CodecFrame f;
    f.data = nv16.data(); f.width = r.w; f.height = r.h;
    f.format = PixelFormat::NV16;
    CheckSame(Convert(f), r.bgra, "NV16");
}

void TestP010MatchesNv12() {
    Reference r = MakeReference(16, 16);
    // 10-bit storage: each sample shifted into the top bits of its 16-bit word,
    // which is the alignment >>8 undoes.
    std::vector<uint8_t> p(static_cast<size_t>(r.w) * r.h * 4, 0);
    uint16_t* py = reinterpret_cast<uint16_t*>(p.data());
    for (int i = 0; i < r.w * r.h; i++) py[i] = static_cast<uint16_t>(r.luma[i] << 8);
    const uint8_t* uv = r.nv12.bytes.data() + static_cast<size_t>(r.w) * r.h;
    uint16_t* pc = reinterpret_cast<uint16_t*>(p.data() + static_cast<size_t>(r.w) * r.h * 2);
    for (int i = 0; i < r.w * (r.h / 2); i++)
        pc[i] = static_cast<uint16_t>(uv[i] << 8);

    CodecFrame p010;
    p010.data = p.data(); p010.width = r.w; p010.height = r.h;
    p010.format = PixelFormat::P010;
    CheckSame(Convert(p010), r.bgra, "P010");

    // P016 is the same storage and the same top alignment, so it must agree.
    CodecFrame p016 = p010;
    p016.format = PixelFormat::P016;
    CheckSame(Convert(p016), r.bgra, "P016");
}

void TestP210MatchesNv16() {
    Reference r = MakeReference(16, 16);
    std::vector<uint8_t> p(static_cast<size_t>(r.w) * r.h * 4, 0);
    uint16_t* py = reinterpret_cast<uint16_t*>(p.data());
    for (int i = 0; i < r.w * r.h; i++) py[i] = static_cast<uint16_t>(r.luma[i] << 8);
    // Full-height interleaved chroma, then shifted into the top bits.
    const uint8_t* uv = r.nv12.bytes.data() + static_cast<size_t>(r.w) * r.h;
    uint16_t* pc = reinterpret_cast<uint16_t*>(p.data() + static_cast<size_t>(r.w) * r.h * 2);
    for (int row = 0; row < r.h; row++)
        for (int i = 0; i < r.w; i++)
            pc[static_cast<size_t>(row) * r.w + i] =
                static_cast<uint16_t>(uv[static_cast<size_t>(row / 2) * r.w + i] << 8);
    CodecFrame f;
    f.data = p.data(); f.width = r.w; f.height = r.h;
    f.format = PixelFormat::P210;
    // P210 duplicates chroma rows like NV16, so it lands on the NV12 reference
    // exactly as NV16 does.
    CheckSame(Convert(f), r.bgra, "P210");
}

void TestYuv444MatchesNv12() {
    Reference r = MakeReference(16, 16);
    // 4:4:4 keeps every chroma sample; nearest-neighbour upsample of the 4:2:0
    // reference reproduces what the converter's col/2 row/2 lookup will pick.
    std::vector<uint8_t> y444(static_cast<size_t>(r.w) * r.h * 3, 0);
    std::memcpy(y444.data(), r.luma.data(), static_cast<size_t>(r.w) * r.h);
    uint8_t* cb = y444.data() + static_cast<size_t>(r.w) * r.h;
    uint8_t* cr = cb + static_cast<size_t>(r.w) * r.h;
    for (int row = 0; row < r.h; row++)
        for (int col = 0; col < r.w; col++) {
            const int src = (row / 2) * (r.w / 2) + col / 2;
            cb[static_cast<size_t>(row) * r.w + col] = r.chroma[src].first;
            cr[static_cast<size_t>(row) * r.w + col] = r.chroma[src].second;
        }
    CodecFrame f;
    f.data = y444.data(); f.width = r.w; f.height = r.h;
    f.format = PixelFormat::YUV444P;
    CheckSame(Convert(f), r.bgra, "YUV444P");
}

void TestGrayIsLumaAtNeutralChroma() {
    Reference r = MakeReference(16, 16);
    CodecFrame f;
    f.data = r.luma.data(); f.width = r.w; f.height = r.h;
    f.format = PixelFormat::GRAY;
    const std::vector<uint8_t> got = Convert(f);
    bool ok = true;
    for (int row = 0; row < r.h && ok; row++)
        for (int col = 0; col < r.w && ok; col++) {
            const Rgb want = Bt601(r.luma[row * r.w + col], 128, 128);
            const Rgb p = PixelAt(got, r.w, row, col);
            if (p.r != want.r || p.g != want.g || p.b != want.b) ok = false;
        }
    CHECK(ok);
}

// --- Contract edges -------------------------------------------------------

void TestZeroStridesMeanTight() {
    // CodecFrame leaves strides{} zero for "tightly packed", so a frame that
    // never sets them must convert exactly like one that spells the width out.
    // Forwarded verbatim instead of resolved, 0 collapses every row onto row 0.
    Reference r = MakeReference(16, 16);
    CodecFrame tight = r.nv12.frame;
    CHECK_EQ(tight.strides[0], 0u);
    CodecFrame explicitStrides = r.nv12.frame;
    explicitStrides.strides[0] = r.w;
    explicitStrides.strides[1] = r.w;
    CheckSame(Convert(tight), Convert(explicitStrides), "zero vs explicit strides");
    CheckSame(Convert(tight), r.bgra, "zero strides vs reference");
}

void TestPaddedRowsAreHonoured() {
    // A decoder is free to pad rows (that is what a stride is for). Pad both
    // planes and confirm the picture is unchanged -- with the padding poisoned,
    // so a converter that ignores the stride reads the wrong bytes.
    const int w = 16, h = 16, yPad = 7, cPad = 5;
    Reference r = MakeReference(w, h);
    std::vector<uint8_t> padded(static_cast<size_t>(w + yPad) * h +
                                static_cast<size_t>(w + cPad) * (h / 2), 0xAB);
    for (int row = 0; row < h; row++)
        std::memcpy(padded.data() + static_cast<size_t>(row) * (w + yPad),
                    r.luma.data() + static_cast<size_t>(row) * w, w);
    const uint8_t* uv = r.nv12.bytes.data() + static_cast<size_t>(w) * h;
    uint8_t* puv = padded.data() + static_cast<size_t>(w + yPad) * h;
    for (int row = 0; row < h / 2; row++)
        std::memcpy(puv + static_cast<size_t>(row) * (w + cPad),
                    uv + static_cast<size_t>(row) * w, w);
    CodecFrame f;
    f.data = padded.data(); f.width = w; f.height = h;
    f.format = PixelFormat::NV12;
    f.strides[0] = w + yPad;
    f.strides[1] = w + cPad;
    CheckSame(Convert(f), r.bgra, "padded rows");
}

void TestUnsupportedFormatsAreRefused() {
    // Not "converted badly": refused, so the caller can say so instead of
    // uploading pixels it cannot vouch for. BAYER16LE is the interesting one --
    // it is 16-bit data that could be misread as a 16-bit YUV format.
    CodecFrame f;
    std::vector<uint8_t> bytes(4096, 0x40);
    f.data = bytes.data(); f.width = 16; f.height = 16;
    const PixelFormat refused[] = {
        PixelFormat::BAYER16LE, PixelFormat::BGRA, PixelFormat::ARGB,
        PixelFormat::RGBA,      PixelFormat::RGB,  PixelFormat::BGR,
        PixelFormat::Unknown,
    };
    for (PixelFormat pf : refused) {
        f.format = pf;
        bool ok = true;
        Convert(f, &ok);
        CHECK_EQ(ok, false);
    }
    // And a frame with no host pixels is refused too, rather than dereferenced.
    f.format = PixelFormat::NV12;
    f.data = nullptr;
    bool ok = true;
    Convert(f, &ok);
    CHECK_EQ(ok, false);
}

void TestFormatNamesCoverEveryEnum() {
    // hal_player prints this in its diagnostic, so an unnamed format would
    // report itself as "unknown" and be confused with Unknown.
    const PixelFormat all[] = {
        PixelFormat::NV12, PixelFormat::P010, PixelFormat::I420, PixelFormat::P016,
        PixelFormat::NV16, PixelFormat::P210, PixelFormat::YUV444P,
        PixelFormat::YUV444P10LE, PixelFormat::YUV444P16LE, PixelFormat::RGB,
        PixelFormat::BGR, PixelFormat::GRAY, PixelFormat::GRAY10LE, PixelFormat::BGRA,
        PixelFormat::ARGB, PixelFormat::RGBA, PixelFormat::BAYER16LE,
    };
    for (PixelFormat pf : all) {
        const std::string name = halplayer::FormatName(pf);
        CHECK(name != "unknown");
        CHECK(!name.empty());
    }
    CHECK_EQ(std::string(halplayer::FormatName(PixelFormat::Unknown)), std::string("unknown"));
}

} // namespace

int main() {
    std::printf("yuv_to_bgra_test\n");
    TestNeutralChromaIsLuma();
    TestChromaIsUnsigned();
    TestLumaSpanAndClamp();
    TestI420MatchesNv12();
    TestNv16MatchesNv12();
    TestP010MatchesNv12();
    TestP210MatchesNv16();
    TestYuv444MatchesNv12();
    TestGrayIsLumaAtNeutralChroma();
    TestZeroStridesMeanTight();
    TestPaddedRowsAreHonoured();
    TestUnsupportedFormatsAreRefused();
    TestFormatNamesCoverEveryEnum();
    return haltest::finish("yuv_to_bgra_test");
}
