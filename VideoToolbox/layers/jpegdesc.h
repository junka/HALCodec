#ifndef LAYERS_JPEGDESC_H_
#define LAYERS_JPEGDESC_H_

#include <cstddef>
#include <cstdint>

namespace halcodec {
namespace vtbox {

// One JPEG codestream found inside a buffer, plus what its frame header says.
//
// An elementary video stream states its picture size in a parameter set the
// decoder can hand to VideoToolbox; a JPEG carries it in a marker segment
// instead, and there is no call that builds a format description from JPEG
// bytes, so the size has to be read out here before a session can be opened.
struct JpegCodestream {
    size_t start = 0;    // offset of the SOI marker (FF D8)
    size_t size = 0;     // SOI through EOC (FF D9) inclusive, or through the
                         // end of the buffer when the stream is truncated
    bool complete = false;  // false when the entropy data never reached its EOC
    int width = 0;
    int height = 0;
    int precision = 0;   // sample precision from the frame header (8 for baseline)
    int componentCount = 0;
    // True for 4:2:0 chroma (one luma pair sampled twice in each direction):
    // the only subsampling a VideoToolbox JPEG decode has been measured against
    // here, and the only one whose output buffer matches a two-plane NV12 frame.
    bool subsampled420 = false;
};

// True when the buffer opens with a JPEG start-of-image marker.
bool LooksLikeJpeg(const uint8_t* data, size_t size);

// Locates the first codestream in `data` and reads its frame header. Returns
// true when the picture is described, including when its last bytes have not
// arrived yet (`complete` false, the span running to the end of the buffer);
// false when nothing here could open a session -- no start of image, a marker
// chain that stops making sense, or no frame header in what has arrived.
//
// The walk is marker-segment based rather than a search for the next FF D9:
// entropy-coded data may contain 0xFF D9-looking pairs only after byte
// unstuffing, and an EXIF APP1 segment may hold a whole embedded thumbnail
// JPEG, whose markers would end the scan far too early.
bool FindJpegCodestream(const uint8_t* data, size_t size, JpegCodestream* out);

}  // namespace vtbox
}  // namespace halcodec

#endif  // LAYERS_JPEGDESC_H_
