#ifndef LAYERS_AV1DESC_H_
#define LAYERS_AV1DESC_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace halcodec {
namespace vtbox {

// AV1 elementary-stream front end: cuts an OBU stream into temporal units,
// reads a sequence header, and turns one into the `av01` sample entry
// VideoToolbox needs to open an AV1 session. Pure bitstream bookkeeping with no
// vendor framework in it, which is what lets the ingest rules be tested alone.

// OBU types of 4.11 (Table 3). Only the ones an ingest path has to recognise.
enum Av1ObuType {
    kAv1ObuSequenceHeader = 1,
    kAv1ObuTemporalDelimiter = 2,
    kAv1ObuFrame = 7
};

// True when the buffer opens with an OBU_TEMPORAL_DELIMITER, which is how an AV1
// OBU stream announces itself: the byte is not a plausible H.264 or HEVC NAL
// header under either reading (their parameter-set types never map onto 2).
bool IsAv1ObuStream(const uint8_t* data, size_t size);

// One OBU inside a scanned buffer.
struct Av1Obu {
    int type;
    size_t start;  // offset of the header byte
    size_t size;   // whole OBU: header, optional extension and size field, body
    // Set when obu_has_size_field was 0, so `size` is only "up to the end of the
    // buffer" and the real extent is the end of the temporal unit.
    bool runsToEnd;
};

// Walks at most `maxObus` OBUs from the start of the buffer, stopping at the
// first malformed one. An OBU whose header promises more bytes than the buffer
// holds is reported with the truncated extent, so a caller feeding chunks can
// tell "incomplete" from "corrupt".
std::vector<Av1Obu> ScanObus(const uint8_t* data, size_t size, size_t maxObus);

// One temporal unit as a half-open byte range. AV1 has no start codes: a
// temporal unit is an OBU_TEMPORAL_DELIMITER and everything up to the next one.
struct Av1TemporalUnit {
    size_t firstByte;
    size_t endByte;
};

// Groups [0, size) into temporal units, at most `maxUnits` of them, by walking
// at most `maxObus` OBUs. The unit still open at the limit is only returned when
// `closeOpen` says no further input will arrive to bound it. OBUs ahead of the
// first delimiter belong to no unit; bytes that do not parse as an OBU stop the
// walk, because AV1 offers no start code to resynchronise on. `*scanTruncated`
// reports that the OBU bound cut the walk off, which is what tells a
// chunk-feeding caller that even `closeOpen` would not make the trailing range a
// complete unit.
std::vector<Av1TemporalUnit> GroupAv1TemporalUnits(const uint8_t* data,
                                                   size_t size, size_t maxUnits,
                                                   size_t maxObus, bool closeOpen,
                                                   bool* scanTruncated = nullptr);

// Everything a sample entry needs from an OBU_SEQUENCE_HEADER. The field walk
// covers sequence_header() in full; the values themselves are only ever produced
// for the parameter encodings this build claims to decode.
struct Av1SequenceHeader {
    int profile = -1;
    int levelIdx = 0;      // seq_level_idx[0]
    int bitDepth = 8;
    bool monoChrome = false;
    int chromaSubX = 1;    // profile 0 -> 4:2:0
    int chromaSubY = 1;
    int chromaSamplePosition = 0;
    int width = 0;         // max_frame_width
    int height = 0;

    // Non-null when the header parsed cleanly but this build cannot hand it to
    // VideoToolbox; carries the reason for the caller's log. The encodings that
    // land here are the ones no sample available locally can exercise, so they
    // are refused rather than guessed at.
    const char* unsupported = nullptr;
};

// Parses an OBU_SEQUENCE_HEADER including its OBU header byte, LEB128 size and
// optional extension header. Returns false when the header is truncated or
// inconsistent (the syntax element count does not close on the trailing bits);
// a header that parses but is not supported returns true with `unsupported` set.
bool ParseAv1SequenceHeader(const uint8_t* obu, size_t size,
                            Av1SequenceHeader* out);

// Builds a complete `av01` VisualSampleEntry box -- 4-byte size and type
// included -- with a single `av1C` child carrying the AV1CodecConfigurationRecord
// and `configObus` (the sequence header OBUs, may be empty). This is the byte
// layout CMVideoFormatDescriptionCreateFromBigEndianImageDescriptionData()
// consumes; VideoToolbox rejects the session without the av1C record, and
// validates its profile, bit depth, mono and chroma subsampling against the
// stream while ignoring the level value.
std::vector<uint8_t> BuildAv01SampleEntry(const Av1SequenceHeader& header,
    const uint8_t* configObus, size_t configObusSize);

} // namespace vtbox
} // namespace halcodec

#endif // LAYERS_AV1DESC_H_
