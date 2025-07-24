#pragma once
//Implementation of https://arxiv.org/html/2505.17259v1#:~:text=The%20core%20of%20the%20described,and%20the%20predefined%20key%20representations.
// Got the table from https://emusicology.org/index.php/EMR/article/view/5916/4785
//We start with Krumhansl–Kessler tonal‐hierarchy profiles, this will give us how in key the sound is to C major or C minor
//Specificly we will do cosine similarity to see which key the audio matches the best to
#include <array>
#include <vector>
#include <string>

namespace KeyDetection
{

    extern const std::array<double, 12> C_MAJOR_RAW;
    extern const std::array<double, 12> C_MINOR_RAW;

    // Compute the unit-length mean-chroma from a mono audio buffer
    std::array<double, 12> getMeanChroma(const std::vector<double>& audio, int Fs);

    // Build all 24 key profiles (first 12 majors, next 12 minors)
    void buildKeyProfiles(
        std::array<std::array<double, 12>, 24>& outProfiles,
        std::array<std::string, 24>& outNames
    );

    // Normalize a 12-vector in-place
    void normalize(std::array<double, 12>& v);

    // Cosine similarity (dot-product) of two 12-vectors
    double cosineSimilarity(
        const std::array<double, 12>& a,
        const std::array<double, 12>& b
    );

    // Detect best key index [0..23] given a normalized chroma
    int detectKey(const std::array<double, 12>& normalizedChroma, const std::array<std::array<double, 12>, 24>& profiles);
}