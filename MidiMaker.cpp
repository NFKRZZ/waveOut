#include "MidiMaker.h"
#include "Keys.h"
#include "Chunk.h"
#include <string>
#include <iostream>
#include <fftw3.h>
#include "Functions.h"
#include "GLOBAL.h"
#include <iomanip>
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include "MidiFile.h"
#include "Options.h"

using namespace std;
using namespace smf;

namespace
{
    constexpr int kMidiExportStemCount = 4;

    static const char* StemTrackName(int stemIndex)
    {
        switch (stemIndex)
        {
        case 0: return "Vocals";
        case 1: return "Drums";
        case 2: return "Bass";
        case 3: return "Chords";
        default: return "Stem";
        }
    }

    static const wchar_t* StemFileSuffix(int stemIndex)
    {
        switch (stemIndex)
        {
        case 0: return L"vocals";
        case 1: return L"drums";
        case 2: return L"bass";
        case 3: return L"chords";
        default: return L"stem";
        }
    }

    static int SecondsToMidiTick(double seconds, const MidiExportOptions& options)
    {
        if (!std::isfinite(seconds) || seconds <= 0.0)
            return 0;
        const double bpm = (std::isfinite(options.bpm) && options.bpm > 0.0) ? options.bpm : 120.0;
        const int tpq = (options.ticksPerQuarter > 0) ? options.ticksPerQuarter : 480;
        const double ticksPerSecond = (static_cast<double>(tpq) * bpm) / 60.0;
        const double tick = std::round(seconds * ticksPerSecond);
        if (tick <= 0.0)
            return 0;
        if (tick >= static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(tick);
    }

    static void AddTempoAndMeterMeta(MidiFile& midi, const MidiExportOptions& options)
    {
        const double bpm = (std::isfinite(options.bpm) && options.bpm > 0.0) ? options.bpm : 120.0;
        const int beatsPerBar = (std::max)(1, options.beatsPerBar);
        midi.addTempo(0, 0, bpm);
        midi.addTimeSignature(0, 0, beatsPerBar, 4);
    }

    static bool WriteMidiFileToPath(MidiFile& midi, const std::filesystem::path& filePath, std::wstring* errorMessage)
    {
        std::error_code ec;
        const std::filesystem::path parent = filePath.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);

        std::ofstream out(filePath, std::ios::binary);
        if (!out.is_open())
        {
            if (errorMessage)
                *errorMessage = L"Couldn't open the MIDI output file for writing.";
            return false;
        }

        midi.sortTracks();
        midi.write(out);
        out.close();
        if (!out.good())
        {
            if (errorMessage)
                *errorMessage = L"Writing the MIDI file failed.";
            return false;
        }

        return true;
    }

    static bool LoadMidiFileFromPath(const std::filesystem::path& filePath, MidiFile& midi, std::wstring* errorMessage)
    {
        std::ifstream in(filePath, std::ios::binary);
        if (!in.is_open())
        {
            if (errorMessage)
                *errorMessage = L"Couldn't open the MIDI file for reading.";
            return false;
        }

        if (!midi.read(in) || !midi.status())
        {
            if (errorMessage)
                *errorMessage = L"The dropped file couldn't be parsed as MIDI.";
            return false;
        }

        midi.doTimeAnalysis();
        midi.linkNotePairs();
        return true;
    }

    static std::wstring WidenMidiTextBestEffort(const std::string& text)
    {
        std::wstring out;
        out.reserve(text.size());
        for (unsigned char ch : text)
            out.push_back(static_cast<wchar_t>(ch));
        return out;
    }

    static std::wstring ExtractTrackName(MidiFile& midi, int trackIndex)
    {
        if (trackIndex < 0 || trackIndex >= midi.getTrackCount())
            return L"";

        for (int eventIndex = 0; eventIndex < midi[trackIndex].size(); ++eventIndex)
        {
            MidiEvent& ev = midi[trackIndex][eventIndex];
            if (!ev.isTrackName())
                continue;

            const std::wstring name = WidenMidiTextBestEffort(ev.getMetaContent());
            if (!name.empty())
                return name;
        }

        return L"";
    }

    static bool IsImportableMidiNoteEvent(const MidiEvent& ev)
    {
        return ev.isNoteOn() && ev.isLinked() && std::isfinite(ev.seconds) && (ev.getDurationInSeconds() > 0.0);
    }
}




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
    int numOfChunks = lowPassData.size() / (sampleSize);

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
    int numOfChunks = bandPassData.size() / (sampleSize);
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

        double highestMagnitudes[6] = { 0.0 };
        unsigned int maxIndices[6] = { 0 };

        for (unsigned int l = 0; l < N; ++l) {
            double magnitude = sqrt(in[l][0] * in[l][0] + in[l][1] * in[l][1]);

            // Check if the magnitude is higher than any of the current top three
            for (int i = 0; i < 6; ++i) {
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
        for (int a = 0;a < 6;a++)
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
    int numOfChunks = static_cast<long long>(highPassData.size()) / (sampleSize);
    cout << "THIS IS THE numOFChunks For HighPass: " << numOfChunks << endl;
    cout << "THIS IS THE size of highPassData: " << highPassData.size() << endl;

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

        double highestMagnitudes[6] = { 0.0 };
        unsigned int maxIndices[6] = { 0 };

        for (unsigned int l = 0; l < N; ++l) {
            double magnitude = sqrt(in[l][0] * in[l][0] + in[l][1] * in[l][1]);

            // Check if the magnitude is higher than any of the current top three
            for (int i = 0; i < 6; ++i) {
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
        for (int a = 0;a < 6;a++)
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


void makeMidi(vector<vector<Chunk>> data)
{

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

bool MidiMaker::inspectMidiFileTracks(const std::filesystem::path& filePath,
    std::vector<MidiImportTrackInfo>& outTracks,
    std::wstring* errorMessage)
{
    outTracks.clear();

    MidiFile midi;
    if (!LoadMidiFileFromPath(filePath, midi, errorMessage))
        return false;

    for (int trackIndex = 0; trackIndex < midi.getTrackCount(); ++trackIndex)
    {
        MidiImportTrackInfo info{};
        info.trackIndex = trackIndex;
        info.name = ExtractTrackName(midi, trackIndex);

        for (int eventIndex = 0; eventIndex < midi[trackIndex].size(); ++eventIndex)
        {
            const MidiEvent& ev = midi[trackIndex][eventIndex];
            if (!IsImportableMidiNoteEvent(ev))
                continue;

            ++info.noteCount;
            const int channel = ev.getChannel();
            if (channel >= 0 && channel < 16)
                info.channelMask = static_cast<std::uint16_t>(info.channelMask | (static_cast<std::uint16_t>(1u) << channel));
        }

        if (info.noteCount <= 0)
            continue;

        if (info.name.empty())
            info.name = L"Track " + std::to_wstring(trackIndex + 1);

        outTracks.push_back(info);
    }

    if (outTracks.empty() && errorMessage)
        *errorMessage = L"The MIDI file doesn't contain any importable note tracks.";

    return true;
}

bool MidiMaker::importMidiFileNotes(const std::filesystem::path& filePath,
    const MidiImportOptions& options,
    std::vector<MidiImportedNote>& outNotes,
    std::wstring* errorMessage)
{
    outNotes.clear();

    MidiFile midi;
    if (!LoadMidiFileFromPath(filePath, midi, errorMessage))
        return false;

    std::vector<bool> selectedTracks(static_cast<std::size_t>(midi.getTrackCount()), options.trackIndices.empty());
    if (!options.trackIndices.empty())
    {
        std::fill(selectedTracks.begin(), selectedTracks.end(), false);
        for (int trackIndex : options.trackIndices)
        {
            if (trackIndex >= 0 && trackIndex < midi.getTrackCount())
                selectedTracks[static_cast<std::size_t>(trackIndex)] = true;
        }
    }

    for (int trackIndex = 0; trackIndex < midi.getTrackCount(); ++trackIndex)
    {
        if (!selectedTracks[static_cast<std::size_t>(trackIndex)])
            continue;

        for (int eventIndex = 0; eventIndex < midi[trackIndex].size(); ++eventIndex)
        {
            const MidiEvent& ev = midi[trackIndex][eventIndex];
            if (!IsImportableMidiNoteEvent(ev))
                continue;

            const int channel = ev.getChannel();
            if (channel >= 0 && channel < 16)
            {
                const std::uint16_t bit = static_cast<std::uint16_t>(static_cast<std::uint16_t>(1u) << channel);
                if ((options.channelMask & bit) == 0)
                    continue;
            }

            MidiImportedNote note{};
            note.startSeconds = ev.seconds;
            note.endSeconds = ev.seconds + ev.getDurationInSeconds();
            note.midiNote = std::clamp(ev.getKeyNumber(), 0, 127);
            note.velocity = std::clamp(ev.getVelocity(), 1, 127);
            note.trackIndex = trackIndex;
            note.channel = std::clamp(channel, 0, 15);
            outNotes.push_back(note);
        }
    }

    std::sort(outNotes.begin(), outNotes.end(), [](const MidiImportedNote& a, const MidiImportedNote& b)
    {
        if (std::fabs(a.startSeconds - b.startSeconds) > 1e-9)
            return a.startSeconds < b.startSeconds;
        if (a.trackIndex != b.trackIndex)
            return a.trackIndex < b.trackIndex;
        if (a.midiNote != b.midiNote)
            return a.midiNote < b.midiNote;
        return a.endSeconds < b.endSeconds;
    });

    return true;
}

bool MidiMaker::exportPlacedNotesToMidiFile(const std::filesystem::path& filePath,
    const std::vector<MidiExportNote>& notes,
    const MidiExportOptions& options,
    std::wstring* errorMessage)
{
    MidiFile midi;
    const int tpq = (options.ticksPerQuarter > 0) ? options.ticksPerQuarter : 480;
    midi.setTicksPerQuarterNote(tpq);
    midi.addTrack(kMidiExportStemCount);
    AddTempoAndMeterMeta(midi, options);

    for (int stemIndex = 0; stemIndex < kMidiExportStemCount; ++stemIndex)
    {
        const int track = stemIndex + 1;
        midi.addTrackName(track, 0, StemTrackName(stemIndex));
    }

    for (const MidiExportNote& note : notes)
    {
        const int stemIndex = std::clamp(note.stemIndex, 0, kMidiExportStemCount - 1);
        const int track = stemIndex + 1;
        const int channel = stemIndex;
        const int key = std::clamp(note.midiNote, 0, 127);
        const int velocity = std::clamp(note.velocity, 1, 127);
        const int startTick = SecondsToMidiTick(note.startSeconds, options);
        int endTick = SecondsToMidiTick(note.endSeconds, options);
        if (endTick <= startTick)
            endTick = startTick + 1;
        midi.addNoteOn(track, startTick, channel, key, velocity);
        midi.addNoteOff(track, endTick, channel, key);
    }

    return WriteMidiFileToPath(midi, filePath, errorMessage);
}

bool MidiMaker::exportPlacedNotesToStemMidiFiles(const std::filesystem::path& basePath,
    const std::vector<MidiExportNote>& notes,
    const MidiExportOptions& options,
    std::vector<std::filesystem::path>* writtenPaths,
    std::wstring* errorMessage)
{
    if (writtenPaths)
        writtenPaths->clear();

    std::array<std::vector<MidiExportNote>, kMidiExportStemCount> notesByStem;
    for (const MidiExportNote& note : notes)
    {
        const int stemIndex = std::clamp(note.stemIndex, 0, kMidiExportStemCount - 1);
        notesByStem[stemIndex].push_back(note);
    }

    std::filesystem::path stemBase = basePath;
    if (stemBase.extension().empty())
        stemBase.replace_extension(L".mid");

    for (int stemIndex = 0; stemIndex < kMidiExportStemCount; ++stemIndex)
    {
        MidiFile midi;
        const int tpq = (options.ticksPerQuarter > 0) ? options.ticksPerQuarter : 480;
        midi.setTicksPerQuarterNote(tpq);
        AddTempoAndMeterMeta(midi, options);
        midi.addTrackName(0, 0, StemTrackName(stemIndex));

        for (const MidiExportNote& note : notesByStem[stemIndex])
        {
            const int key = std::clamp(note.midiNote, 0, 127);
            const int velocity = std::clamp(note.velocity, 1, 127);
            const int startTick = SecondsToMidiTick(note.startSeconds, options);
            int endTick = SecondsToMidiTick(note.endSeconds, options);
            if (endTick <= startTick)
                endTick = startTick + 1;
            midi.addNoteOn(0, startTick, 0, key, velocity);
            midi.addNoteOff(0, endTick, 0, key);
        }

        std::filesystem::path stemPath = stemBase;
        stemPath.replace_filename(stemBase.stem().wstring() + L"_" + StemFileSuffix(stemIndex) + stemBase.extension().wstring());
        if (!WriteMidiFileToPath(midi, stemPath, errorMessage))
            return false;
        if (writtenPaths)
            writtenPaths->push_back(stemPath);
    }

    return true;
}

