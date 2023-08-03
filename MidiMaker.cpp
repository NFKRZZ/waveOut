#include "MidiMaker.h"
#include "Keys.h"
#include "Chunk.h"
#include <string>
#include <iostream>
#include <fftw3.h>
#include "Functions.h"
#include "GLOBAL.h"
#include <iomanip>
#include "MidiFile.h"
#include "Options.h"

using namespace std;
using namespace smf;




FILE* Init(string filename)
{
    FILE* file;
    errno_t error = fopen_s(&file, filename.c_str(), "w");

    if (!file)
    {
        std::cerr << "Failed to create file" << std::endl;
    }

    return file;
}

vector<Chunk> MidiMaker::lowPass(vector<short int> lowPassData)
{
    int sampleSize = GLOBAL::twoBeatDuration * GLOBAL::sampleRate;
    int numOfChunks = lowPassData.size() / (sampleSize * 2);

    vector<vector<double>> sampleChunks;
    sampleChunks.resize(numOfChunks);
    vector<Chunk> chunkData;
    for (int i = 0; i < numOfChunks;i++)
    {
        for (int j = 0;j < sampleSize;j++)
        {
            sampleChunks[i].push_back(lowPassData[(i * sampleSize) + j]);
        }
    }
    //Do FFT
    for (int i = 0; i < numOfChunks;i++)
    {
        int N = sampleChunks[i].size();
        fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);

        for (int j = 0;j < N;j++)
        {
            in[j][0] = sampleChunks[i][j];
            in[j][1] = 0;
        }
        fftw_plan plan = fftw_plan_dft_1d(N, in, in, FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(plan);

        double highestMagnitudes[3] = { 0.0 };
        unsigned int maxIndices[3] = { 0 };

        for (unsigned int l = 0; l < N; ++l) {
            double magnitude = sqrt(in[l][0] * in[l][0] + in[l][1] * in[l][1]);

            // Check if the magnitude is higher than any of the current top three
            for (int i = 0; i < 3; ++i) {
                if (magnitude > highestMagnitudes[i]) {
                    // Shift the current values down the array to make room for the new magnitude
                    for (int j = 2; j > i; --j) {
                        highestMagnitudes[j] = highestMagnitudes[j - 1];
                        maxIndices[j] = maxIndices[j - 1];
                    }

                    // Store the new magnitude and index
                    highestMagnitudes[i] = magnitude;
                    maxIndices[i] = l;

                    break;  // No need to check the remaining elements
                }
            }
        }

        int sampleRate = GLOBAL::sampleRate;

        


        /*for (int ja = 0; ja < 3; ++ja) 
        {
            double frequency = static_cast<double>(maxIndices[ja]) * sampleRate / N;
            cout << i<<" Frequency " << ja + 1 << ": " << frequency << " Hz, Magnitude: " << highestMagnitudes[ja] << endl;
        }*/
        vector<double> Frequencies;
        vector<double> mag;
        for (int a = 0;a < 3;a++)
        {
            double freq = (double)maxIndices[a] * sampleRate / N;
            if (freq > sampleRate / 2)
            {
                freq = abs(freq - sampleRate);
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }
            else
            {
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }

        }
        Chunk c = Chunk(Frequencies, mag, i,GLOBAL::twoBeatDuration);
        c.Init();
        chunkData.push_back(c);
    }

    std::cout << "This is the size of lowpass chunk vector: " << chunkData.size()<<endl;
    return chunkData;

}

vector<Chunk> MidiMaker::bandPass(vector<short int> bandPassData)
{
    int sampleSize = GLOBAL::qBeatDuration * GLOBAL::sampleRate;
    int numOfChunks = bandPassData.size() / (sampleSize * 2);

    vector<vector<double>> sampleChunks;
    sampleChunks.resize(numOfChunks);
    vector<Chunk> chunkData;
    for (int i = 0; i < numOfChunks;i++)
    {
        for (int j = 0;j < sampleSize;j++)
        {
            sampleChunks[i].push_back(bandPassData[(i * sampleSize) + j]);
        }
    }
    //Do FFT
    for (int i = 0; i < numOfChunks;i++)
    {
        int N = sampleChunks[i].size();
        fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);

        for (int j = 0;j < N;j++)
        {
            in[j][0] = sampleChunks[i][j];
            in[j][1] = 0;
        }
        fftw_plan plan = fftw_plan_dft_1d(N, in, in, FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(plan);

        double highestMagnitudes[3] = { 0.0 };
        unsigned int maxIndices[3] = { 0 };

        for (unsigned int l = 0; l < N; ++l) {
            double magnitude = sqrt(in[l][0] * in[l][0] + in[l][1] * in[l][1]);

            // Check if the magnitude is higher than any of the current top three
            for (int i = 0; i < 3; ++i) {
                if (magnitude > highestMagnitudes[i]) {
                    // Shift the current values down the array to make room for the new magnitude
                    for (int j = 2; j > i; --j) {
                        highestMagnitudes[j] = highestMagnitudes[j - 1];
                        maxIndices[j] = maxIndices[j - 1];
                    }

                    // Store the new magnitude and index
                    highestMagnitudes[i] = magnitude;
                    maxIndices[i] = l;

                    break;  // No need to check the remaining elements
                }
            }
        }

        int sampleRate = GLOBAL::sampleRate;




        /*for (int ja = 0; ja < 3; ++ja)
        {
            double frequency = static_cast<double>(maxIndices[ja]) * sampleRate / N;
            cout << i<<" Frequency " << ja + 1 << ": " << frequency << " Hz, Magnitude: " << highestMagnitudes[ja] << endl;
        }*/
        vector<double> Frequencies;
        vector<double> mag;
        for (int a = 0;a < 3;a++)
        {
            double freq = (double)maxIndices[a] * sampleRate / N;
            if (freq > sampleRate / 2)
            {
                freq = abs(freq - sampleRate);
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }
            else
            {
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }

        }
        Chunk c = Chunk(Frequencies, mag, i,GLOBAL::qBeatDuration);
        c.Init();
        chunkData.push_back(c);
    }

    std::cout << "This is the size of bandpass chunk vector: " << chunkData.size() << endl;
    return chunkData;

}

vector<Chunk> MidiMaker::highPass(vector<short int> highPassData)
{
    int sampleSize = GLOBAL::qBeatDuration * GLOBAL::sampleRate;
    cout << "THIS IS SAMPLE SIZE HIGH PASS MIDI: " << sampleSize << endl;
    int numOfChunks = highPassData.size() / (sampleSize * 2);

    vector<vector<double>> sampleChunks;
    sampleChunks.resize(numOfChunks);
    vector<Chunk> chunkData;
    for (int i = 0; i < numOfChunks;i++)
    {
        for (int j = 0;j < sampleSize;j++)
        {
            sampleChunks[i].push_back(highPassData[(i * sampleSize) + j]);
        }
    }
    //Do FFT
    for (int i = 0; i < numOfChunks;i++)
    {
        int N = sampleChunks[i].size();
        fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);

        for (int j = 0;j < N;j++)
        {
            in[j][0] = sampleChunks[i][j];
            in[j][1] = 0;
        }
        fftw_plan plan = fftw_plan_dft_1d(N, in, in, FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(plan);

        double highestMagnitudes[3] = { 0.0 };
        unsigned int maxIndices[3] = { 0 };

        for (unsigned int l = 0; l < N; ++l) {
            double magnitude = sqrt(in[l][0] * in[l][0] + in[l][1] * in[l][1]);

            // Check if the magnitude is higher than any of the current top three
            for (int i = 0; i < 3; ++i) {
                if (magnitude > highestMagnitudes[i]) {
                    // Shift the current values down the array to make room for the new magnitude
                    for (int j = 2; j > i; --j) {
                        highestMagnitudes[j] = highestMagnitudes[j - 1];
                        maxIndices[j] = maxIndices[j - 1];
                    }

                    // Store the new magnitude and index
                    highestMagnitudes[i] = magnitude;
                    maxIndices[i] = l;

                    break;  // No need to check the remaining elements
                }
            }
        }

        int sampleRate = GLOBAL::sampleRate;




        /*for (int ja = 0; ja < 3; ++ja)
        {
            double frequency = static_cast<double>(maxIndices[ja]) * sampleRate / N;
            cout << i<<" Frequency " << ja + 1 << ": " << frequency << " Hz, Magnitude: " << highestMagnitudes[ja] << endl;
        }*/
        vector<double> Frequencies;
        vector<double> mag;
        for (int a = 0;a < 3;a++)
        {
            double freq = (double)maxIndices[a] * sampleRate / N;
            if (freq > sampleRate / 2)
            {
                freq = abs(freq - sampleRate);
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }
            else
            {
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }

        }
        Chunk c = Chunk(Frequencies, mag, i,GLOBAL::qBeatDuration);
        c.Init();
        chunkData.push_back(c);
    }

    std::cout << "This is the size of highpass chunk vector: " << chunkData.size() << endl;
    return chunkData;

}

void MidiMaker::doSomething()
{
    Options options;
    //options.process(0, 0);
    
    MidiFile midifile;
    std::string filePath = "anda.mid";

        midifile.read(filePath);
    
    midifile.doTimeAnalysis();
    midifile.linkNotePairs();

    int tracks = midifile.getTrackCount();
    cout << "TPQ: " << midifile.getTicksPerQuarterNote() << endl;
    if (tracks > 1) cout << "TRACKS: " << tracks << endl;
    for (int track = 0; track < tracks; track++) {
        if (tracks > 1) cout << "\nTrack " << track << endl;
        cout << "Tick\tSeconds\tDur\tMessage" << endl;
        for (int event = 0; event < midifile[track].size(); event++) {
            cout << dec << midifile[track][event].tick;
            cout << '\t' << dec << midifile[track][event].seconds;
            cout << '\t';
            if (midifile[track][event].isNoteOn())
                cout << midifile[track][event].getDurationInSeconds();
            cout << '\t' << hex;
            for (int i = 0; i < midifile[track][event].size(); i++)
                cout << (int)midifile[track][event][i] << ' ';
            cout << endl;
        }
    }
}

