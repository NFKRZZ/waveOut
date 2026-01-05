#include "KeyDetection.h"

#include <fftw3.h>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace KeyDetection {



    static Key mapKeyfinderToMajorKeyEnum(KeyFinder::key_t k)
    {
        using KeyFinder::key_t;

        // Majors (libKeyFinder uses flats for 5 keys)
        switch (k)
        {
        case KeyFinder::C_MAJOR:        return Key::C_MAJOR;
        case KeyFinder::D_FLAT_MAJOR:   return Key::C_SHARP_MAJOR; // Db -> C#
        case KeyFinder::D_MAJOR:        return Key::D_MAJOR;
        case KeyFinder::E_FLAT_MAJOR:   return Key::D_SHARP_MAJOR; // Eb -> D#
        case KeyFinder::E_MAJOR:        return Key::E_MAJOR;
        case KeyFinder::F_MAJOR:        return Key::F_MAJOR;
        case KeyFinder::G_FLAT_MAJOR:   return Key::F_SHARP_MAJOR; // Gb -> F#
        case KeyFinder::G_MAJOR:        return Key::G_MAJOR;
        case KeyFinder::A_FLAT_MAJOR:   return Key::G_SHARP_MAJOR; // Ab -> G#
        case KeyFinder::A_MAJOR:        return Key::A_MAJOR;
        case KeyFinder::B_FLAT_MAJOR:   return Key::A_SHARP_MAJOR; // Bb -> A#
        case KeyFinder::B_MAJOR:        return Key::B_MAJOR;

        case KeyFinder::SILENCE:        return Key::NO_KEY;
        default: break;
        }
        // Minors -> relative majors (same pitch-class set; good for scale clamping)
        switch (k)
        {
        case KeyFinder::A_MINOR:        return Key::C_MAJOR;
        case KeyFinder::B_FLAT_MINOR:   return Key::C_SHARP_MAJOR; // Db major
        case KeyFinder::B_MINOR:        return Key::D_MAJOR;
        case KeyFinder::C_MINOR:        return Key::D_SHARP_MAJOR; // Eb major
        case KeyFinder::D_FLAT_MINOR:   return Key::E_MAJOR;
        case KeyFinder::D_MINOR:        return Key::F_MAJOR;
        case KeyFinder::E_FLAT_MINOR:   return Key::F_SHARP_MAJOR; // Gb major
        case KeyFinder::E_MINOR:        return Key::G_MAJOR;
        case KeyFinder::F_MINOR:        return Key::G_SHARP_MAJOR; // Ab major
        case KeyFinder::G_FLAT_MINOR:   return Key::A_MAJOR;
        case KeyFinder::G_MINOR:        return Key::A_SHARP_MAJOR; // Bb major
        case KeyFinder::A_FLAT_MINOR:   return Key::B_MAJOR;
        default:                        return Key::NO_KEY;
        }
    }

    Key getKey(const std::vector<double>& pcmData, int sampleRate,  KeyFinder::KeyFinder& f)
    {
        KeyFinder::AudioData a;
        a.setFrameRate(sampleRate);
        a.setChannels(1);
        a.addToSampleCount(pcmData.size());
        for (int i = 0; i < static_cast<unsigned int>(pcmData.size()); ++i) {
            a.setSample(i, pcmData[i]); // expects normalized-ish audio (e.g., -1..1)
        }
        const KeyFinder::key_t key = f.keyOfAudio(a);
        std::cout << "Lol high" << key << std::endl;
        return mapKeyfinderToMajorKeyEnum(key);

        
    }

 
 }


