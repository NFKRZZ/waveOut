#pragma once
#include <iostream>
#include <vector>
#include <filesystem>
#include <string>
#include <cstdint>
#include "Chunk.h"
using namespace std;

struct MidiExportNote
{
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	int midiNote = 60;
	int velocity = 100;
	int stemIndex = 0;
};

struct MidiExportOptions
{
	double bpm = 120.0;
	int beatsPerBar = 4;
	int ticksPerQuarter = 480;
};

struct MidiImportTrackInfo
{
	int trackIndex = 0;
	std::wstring name;
	int noteCount = 0;
	std::uint16_t channelMask = 0;
};

struct MidiImportOptions
{
	std::vector<int> trackIndices;
	std::uint16_t channelMask = 0xFFFF;
};

struct MidiImportedNote
{
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	int midiNote = 60;
	int velocity = 100;
	int trackIndex = 0;
	int channel = 0;
};

class MidiMaker
{
public:
	MidiMaker()
	{

	}
	static vector<Chunk> lowPass(vector<short int> lowPassData);
	static vector<Chunk> bandPass(vector<short int> bandPassData);
	static vector<Chunk> highPass(vector<short int> highPassData);
	static void doSomething();
	static bool exportPlacedNotesToMidiFile(const std::filesystem::path& filePath,
		const std::vector<MidiExportNote>& notes,
		const MidiExportOptions& options,
		std::wstring* errorMessage = nullptr);
	static bool exportPlacedNotesToStemMidiFiles(const std::filesystem::path& basePath,
		const std::vector<MidiExportNote>& notes,
		const MidiExportOptions& options,
		std::vector<std::filesystem::path>* writtenPaths = nullptr,
		std::wstring* errorMessage = nullptr);
	static bool inspectMidiFileTracks(const std::filesystem::path& filePath,
		std::vector<MidiImportTrackInfo>& outTracks,
		std::wstring* errorMessage = nullptr);
	static bool importMidiFileNotes(const std::filesystem::path& filePath,
		const MidiImportOptions& options,
		std::vector<MidiImportedNote>& outNotes,
		std::wstring* errorMessage = nullptr);

private:

};

