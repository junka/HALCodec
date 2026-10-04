#ifndef APP_ENCODE_CONFIG_H
#define APP_ENCODE_CONFIG_H

// Loads encoder tuning from a JSON config file and applies CLI single-item
// overrides on top. Uses boost::property_tree (Boost is already a project
// dependency) so no new external lib is introduced.
//
// JSON schema (all fields optional; missing => backend default):
//   {
//     "codec": "h264",
//     "preset": "p4",
//     "tuningInfo": "hq",
//     "rateControl": "vbr",
//     "bitrateKbps": 8000,
//     "maxBitrateKbps": 12000,
//     "qp": -1,
//     "quality": 90,
//     "gopLength": 60,
//     "numBFrames": 2,
//     "frameRateNum": 30,
//     "frameRateDen": 1,
//     "profile": "high",
//     "level": "auto",
//     "lowDelay": false
//   }

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <string>

#include "codec_config.h"

namespace halcodec {

// Loads an EncodeConfig from a JSON file path. Missing fields keep their
// sentinel defaults. Returns false (and logs) on read/parse error; the
// EncodeConfig is left partially populated from whatever parsed.
inline bool LoadEncodeConfig(const std::string& path, EncodeConfig& cfg) {
    boost::property_tree::ptree pt;
    try {
        boost::property_tree::read_json(path, pt);
    } catch (const boost::property_tree::json_parser_error& e) {
        std::cerr << "EncodeConfig: failed to parse " << path << ": "
                  << e.what() << std::endl;
        return false;
    }
    // get<T>(key, default) returns default if key absent; we use the current
    // sentinel value as the default so an absent key never overwrites a prior
    // CLI override that the caller may have already set.
    cfg.preset       = pt.get("preset",       cfg.preset);
    cfg.tuningInfo   = pt.get("tuningInfo",   cfg.tuningInfo);
    cfg.rateControl  = pt.get("rateControl",  cfg.rateControl);
    cfg.bitrateKbps  = pt.get("bitrateKbps",  cfg.bitrateKbps);
    cfg.maxBitrateKbps = pt.get("maxBitrateKbps", cfg.maxBitrateKbps);
    cfg.qp           = pt.get("qp",           cfg.qp);
    cfg.quality      = pt.get("quality",      cfg.quality);
    cfg.gopLength    = pt.get("gopLength",    cfg.gopLength);
    cfg.numBFrames   = pt.get("numBFrames",   cfg.numBFrames);
    cfg.frameRateNum = pt.get("frameRateNum", cfg.frameRateNum);
    cfg.frameRateDen = pt.get("frameRateDen", cfg.frameRateDen);
    cfg.profile      = pt.get("profile",      cfg.profile);
    cfg.level        = pt.get("level",        cfg.level);
    cfg.lowDelay     = pt.get("lowDelay",     cfg.lowDelay);
    return true;
}

} // namespace halcodec

#endif // APP_ENCODE_CONFIG_H
