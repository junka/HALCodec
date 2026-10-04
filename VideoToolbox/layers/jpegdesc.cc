#include "jpegdesc.h"

namespace halcodec {
namespace vtbox {

namespace {

constexpr uint8_t kStartOfImage = 0xD8;
constexpr uint8_t kEndOfImage = 0xD9;
constexpr uint8_t kStartOfScan = 0xDA;

// Markers that carry no length field, so the next marker follows immediately.
// T.81 defines only the reserved private marker (0xFF01) and the eight restart
// markers (0xFFD0-0xFFD7) as standing alone; SOI and EOC are handled by name.
bool IsStandaloneMarker(uint8_t marker) {
    return marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7);
}

// Frame accumulation markers (T.81 table K.1): SOF0-SOF3, SOF5-SOF7,
// SOF9-SOF11, SOF13-SOF15. DHT (0xC4), JDTC (0xC8) and DAC (0xCC) fall inside
// that byte range but are not frame headers.
bool IsFrameHeaderMarker(uint8_t marker) {
    return marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
           marker != 0xC8 && marker != 0xCC;
}

uint32_t ReadBe16(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 8) | p[1];
}

}  // namespace

bool LooksLikeJpeg(const uint8_t* data, size_t size) {
    return data && size >= 2 && data[0] == 0xFF && data[1] == kStartOfImage;
}

bool FindJpegCodestream(const uint8_t* data, size_t size, JpegCodestream* out) {
    if (!data || !out || size < 4) {
        return false;
    }
    size_t pos = 0;
    while (pos + 1 < size &&
           !(data[pos] == 0xFF && data[pos + 1] == kStartOfImage)) {
        ++pos;
    }
    if (pos + 1 >= size) {
        return false;  // no start of image anywhere
    }
    JpegCodestream stream;
    stream.start = pos;
    pos += 2;

    // Walks the marker chain. A search for the next FF D9 instead would land on
    // an embedded thumbnail's end marker (EXIF keeps one in a length-prefixed
    // APP1 segment, which this walk steps over whole) or on a FF D9 pair that
    // entropy-coded data only forms after byte stuffing.
    bool inScan = false;
    for (;;) {
        if (inScan) {
            // Inside entropy-coded data every 0xFF is either stuffed (followed
            // by 0x00) or the start of a real marker.
            while (pos + 1 < size) {
                if (data[pos] != 0xFF) {
                    ++pos;
                    continue;
                }
                if (data[pos + 1] == 0x00) {
                    pos += 2;
                    continue;
                }
                if (data[pos + 1] == kEndOfImage) {
                    stream.size = pos + 2 - stream.start;
                    stream.complete = true;
                    *out = stream;
                    return true;
                }
                if (IsStandaloneMarker(data[pos + 1])) {
                    pos += 2;  // restart marker; coding resumes after it
                    continue;
                }
                break;  // a length-carrying marker here means the stream is broken
            }
            break;
        }

        if (pos >= size) {
            break;
        }
        if (data[pos] != 0xFF) {
            // Fill bytes may pad out to the next marker; anything else is not a
            // marker chain that can be followed.
            if (data[pos] != 0x00) {
                break;
            }
            ++pos;
            continue;
        }
        if (pos + 1 >= size) {
            break;
        }
        if (data[pos + 1] == 0xFF) {
            ++pos;  // fill byte ahead of a marker
            continue;
        }
        const uint8_t marker = data[pos + 1];
        if (marker == kEndOfImage) {
            stream.size = pos + 2 - stream.start;
            stream.complete = true;
            *out = stream;
            return true;
        }
        if (marker == kStartOfScan) {
            inScan = true;
            pos += 2;
            continue;
        }
        if (IsStandaloneMarker(marker)) {
            pos += 2;
            continue;
        }
        if (pos + 4 > size) {
            break;
        }
        const uint32_t segmentLength = ReadBe16(data + pos + 2);
        if (segmentLength < 2) {
            break;  // the length includes its own two bytes, so this is garbage
        }
        const size_t bodyStart = pos + 4;
        const size_t bodyEnd = pos + 2 + segmentLength;
        if (bodyEnd > size) {
            break;  // segment runs past what has arrived
        }
        if (IsFrameHeaderMarker(marker) && stream.width == 0 &&
            bodyEnd - bodyStart >= 6) {
            stream.precision = data[bodyStart];
            stream.height = static_cast<int>(ReadBe16(data + bodyStart + 1));
            stream.width = static_cast<int>(ReadBe16(data + bodyStart + 3));
            stream.componentCount = data[bodyStart + 5];
            // Component specs follow: identifier, sampling factors (H in the high
            // nibble, V in the low one), quantisation table. 4:2:0 is Y at 2x2
            // with both chroma components at 1x1.
            const size_t compCount =
                static_cast<size_t>(stream.componentCount);
            if (compCount == 3 && bodyEnd - bodyStart >= 6 + 3 * compCount) {
                const uint8_t* comps = data + bodyStart + 6;
                stream.subsampled420 = comps[1] == 0x22 && comps[4] == 0x11 &&
                                       comps[7] == 0x11;
            }
        }
        pos = bodyEnd;
    }

    // Ran out of buffer (or of valid markers) before the end of image. The
    // caller can still use this if the frame header was reached: the picture is
    // known, only its last bytes are missing.
    if (stream.width <= 0 || stream.height <= 0) {
        return false;
    }
    stream.size = size - stream.start;
    stream.complete = false;
    *out = stream;
    return true;
}

}  // namespace vtbox
}  // namespace halcodec
