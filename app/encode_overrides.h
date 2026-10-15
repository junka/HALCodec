#ifndef APP_ENCODE_CONFIG_H
#define APP_ENCODE_CONFIG_H

// The CLI half of encoder tuning: applies the single-item --preset/--bitrate/...
// overrides on top of an EncodeConfig, loading --enc-config first when it was
// given.
//
// The struct and its JSON loader live in src/ (codec_config.h, encode_config.h)
// because EncodeConfig is the type every backend's Initialize takes; only this
// overlay is app-side, since it is the piece that needs app/parse_cli.h.

#include <string>

#include "codec_config.h"
#include "encode_config.h"
#include "parse_cli.h"

namespace halcodec {

// Load the JSON config named by --enc-config (if any) and apply the CLI's
// single-item overrides on top: CLI wins, and any CLI item left at its sentinel
// is skipped so it cannot clobber a value that came from the file.
//
// The field set is the union of what every encoding app honours, so a flag is
// reachable whichever app is running (hal_cam used to drop --preset/--tuning/
// --quality silently). One copy on purpose: three near-identical blocks had
// already drifted -- hal_enc applied quality, the other two did not.
inline void ApplyEncodeOverrides(const CommandLineParser& cli, EncodeConfig& cfg) {
    if (!cli.getEncodeConfigFile().empty()) {
        LoadEncodeConfig(cli.getEncodeConfigFile(), cfg);
    }
    if (!cli.getPreset().empty())       cfg.preset = cli.getPreset();
    if (!cli.getTuning().empty())       cfg.tuningInfo = cli.getTuning();
    if (!cli.getRateControl().empty())  cfg.rateControl = cli.getRateControl();
    if (cli.getBitrateKbps() >= 0)      cfg.bitrateKbps = cli.getBitrateKbps();
    if (cli.getMaxBitrateKbps() >= 0)   cfg.maxBitrateKbps = cli.getMaxBitrateKbps();
    if (cli.getQp() >= 0)               cfg.qp = cli.getQp();
    if (cli.getQuality() >= 0)          cfg.quality = cli.getQuality();
    if (cli.getGopLength() >= 0)        cfg.gopLength = cli.getGopLength();
    if (cli.getNumBFrames() >= 0)       cfg.numBFrames = cli.getNumBFrames();
    if (cli.getFps() > 0)               cfg.frameRateNum = cli.getFps();
    if (!cli.getProfile().empty())      cfg.profile = cli.getProfile();
    if (!cli.getLevel().empty())        cfg.level = cli.getLevel();
    if (cli.getLowDelay())              cfg.lowDelay = true;
}

} // namespace halcodec

#endif // APP_ENCODE_CONFIG_H
