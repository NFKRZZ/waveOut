#pragma once
#include <array>
#include <vector>
#include <string>
#include <keyfinder/keyfinder.h>
#include "Keys.h"
namespace KeyDetection {

    // Raw K–K probe-tone profiles (C major/minor) – defined in the .cpp
    extern const std::array<double, 12> C_MAJOR_RAW;
    extern const std::array<double, 12> C_MINOR_RAW;

    // Compute a unit-length mean chroma from mono audio at sample rate Fs.
    // (Internally band-limits to reduce hi-hat fizz and sub-rumble.)
    std::array<double, 12> getMeanChroma(const std::vector<double>& audio, int Fs);


    Key getKey(const std::vector<double>& pcmData, int sampleRate, KeyFinder::KeyFinder& f);


    // Compute a unit-length mean chroma restricted to [minFreq, maxFreq] (Hz).
    std::array<double, 12> getMeanChromaBand(const std::vector<double>& audio,
        int Fs, double minFreq, double maxFreq);

    // Build 24 normalized key profiles (12 majors, then 12 minors) and their names.
    void buildKeyProfiles(
        std::array<std::array<double, 12>, 24>& outProfiles,
        std::array<std::string, 24>& outNames
    );

    // Utilities
    void   normalize(std::array<double, 12>& v);
    double cosineSimilarity(const std::array<double, 12>& a,
        const std::array<double, 12>& b);
    int    detectKey(const std::array<double, 12>& normalizedChroma,
        const std::array<std::array<double, 12>, 24>& profiles);

    // Diagnostics / analysis helpers
    void printTopKeys(const std::array<double, 12>& chroma,
        const std::array<std::array<double, 12>, 24>& profiles,
        const std::array<std::string, 24>& names,
        int k = 5);

    struct KeyVoteSummary {
        int best = -1;                 // index of most-voted key (0..23)
        double stability = 0.0;        // votes(best) / total_windows
        std::array<int, 24> counts{};   // histogram of window votes
    };

    KeyVoteSummary voteKeysOverTime(const std::vector<double>& audio, int Fs,
        const std::array<std::array<double, 12>, 24>& profiles,
        const std::array<std::string, 24>& names,
        double winSec = 3.0, double hopSec = 1.5);

    // Bass + Third aware final decision:
    //   score = cosine + lambda * BassPrior(I,V,IV) + mu * ThirdConsistency
    // lambda ~0.20–0.35, mu ~0.08–0.18 work well on EDM.
    int detectKeyBassAware(const std::vector<double>& audio, int Fs,
        const std::array<std::array<double, 12>, 24>& profiles,
        const std::array<std::string, 24>& names,
        double lambda = 0.28, double mu = 0.12);


    


} // namespace KeyDetection
