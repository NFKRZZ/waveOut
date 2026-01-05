#include "BPMDetection.h"
#include <algorithm>
#include <limits>
#include <iostream>

extern "C"
{
    #include <aubio/aubio.h>
}


double BPMDetection::getBpmMonoAubio(const std::vector<double>& monoPcm, int sampleRate)
{
    if (sampleRate <= 0 || monoPcm.empty()) return 0.0;

    const uint_t win_size = 1024;
    const uint_t hop_size = win_size / 4;

    aubio_tempo_t* tempo = new_aubio_tempo("default", win_size, hop_size, (uint_t)sampleRate);
    if (!tempo) return 0.0;

    fvec_t* in = new_fvec(hop_size);
    fvec_t* out = new_fvec(1);

    std::vector<double> beatTimes;
    beatTimes.reserve(256);

    size_t pos = 0;
    while (pos < monoPcm.size())
    {
        for (uint_t i = 0; i < hop_size; ++i)
        {
            if (pos < monoPcm.size())
                in->data[i] = (smpl_t)monoPcm[pos++];
            else
                in->data[i] = 0;
        }

        aubio_tempo_do(tempo, in, out);

        //Beat detected when out->data[0] != 0
        if (out->data[0] != 0)
            beatTimes.push_back((double)aubio_tempo_get_last_s(tempo));
    }

    double bpm = 0.0;
    if (beatTimes.size() >= 2)
    {
        std::vector<double> periods;
        periods.reserve(beatTimes.size() - 1);
        for (size_t i = 1; i < beatTimes.size(); ++i)
        {
            const double dt = beatTimes[i] - beatTimes[i - 1];
            if (dt > 1e-4) periods.push_back(dt);
        }

        if (!periods.empty())
        {
            std::sort(periods.begin(), periods.end());
            const double medianPeriod = periods[periods.size() / 2];
            bpm = 60.0 / medianPeriod;
        }
    }
	std::cout << "Detected BPM using median period: " << bpm << "\n";
    //Fallback
    if (bpm <= 0.0)
		std::cout << "Warning: BPMDetection::getBpmMonoAubio - unable to detect BPM reliably.\n";
        bpm = (double)aubio_tempo_get_bpm(tempo);

	std::cout << "Detected BPM using aubio_get_bpm: " << (double)aubio_tempo_get_bpm(tempo) << "\n";
    while (bpm > 180.0) bpm *= 0.5;
    while (bpm > 0.0 && bpm < 80.0) bpm *= 2.0;

    del_fvec(in);
    del_fvec(out);
    del_aubio_tempo(tempo);

    return bpm;
}