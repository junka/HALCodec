// hal_push -- producer for hal_player's live shm mode (app/shm_ring.h).
//
//   ./hal_player shm:demo --bench          # player: ring host, waits, plays
//   ./hal_push demo stream.hevc            # this: stream the file, exit
//
// Bytes go in as fast as the player consumes them; when the ring fills,
// write() blocks on the space-eventfd until the player drains. --sleep-us
// paces per 256KiB chunk to simulate a capture rate. The input is an
// Annex-B elementary stream (h264/hevc) whose head carries the parameter
// sets -- the player bootstraps its decoder from them.

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "shm_ring.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: hal_push <name> <file.h264|file.hevc|-> [--sleep-us N]\n"
                             "  streams the Annex-B file into the player's shm:NAME ring\n");
        return 1;
    }
    std::string name = argv[1];
    std::string file = argv[2];
    useconds_t sleepUs = 0;
    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "--sleep-us") == 0 && i + 1 < argc) {
            sleepUs = static_cast<useconds_t>(std::atoi(argv[++i]));
        }
    }

    halcodec::ShmRingWriter ring;
    std::string err;
    if (!ring.connect(name, &err)) {
        std::fprintf(stderr, "hal_push: %s\n", err.c_str());
        return 1;
    }

    FILE* in = file == "-" ? stdin : std::fopen(file.c_str(), "rb");
    if (!in) {
        std::fprintf(stderr, "hal_push: cannot open %s\n", file.c_str());
        return 1;
    }

    std::vector<uint8_t> buf(256 * 1024);
    uint64_t total = 0;
    size_t n;
    while ((n = std::fread(buf.data(), 1, buf.size(), in)) > 0) {
        if (!ring.write(buf.data(), n)) {
            std::fprintf(stderr, "hal_push: player went away after %llu bytes\n",
                         static_cast<unsigned long long>(total));
            if (in != stdin) std::fclose(in);
            return 1;
        }
        total += n;
        if (sleepUs) usleep(sleepUs);
    }
    if (in != stdin) std::fclose(in);
    ring.finish();
    std::fprintf(stderr, "hal_push: %s done, %llu bytes\n", name.c_str(),
                 static_cast<unsigned long long>(total));
    return 0;
}
