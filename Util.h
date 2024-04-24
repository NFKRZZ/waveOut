#pragma once
#include <vector>
#include <string>
#include <ios>
#include <fstream>
#include "Keys.h"
#include "GLOBAL.h"
using namespace std;
class Util
{
	struct WAVE_HEADER
	{
		char Chunk[4];
		int ChunkSize;
		char format[4];
		char Sub_chunk1ID[4];
		int Sub_chunk1Size;
		short int AudioFormat;
		short int NumChannels;
		int SampleRate;
		int ByteRate;
		short int BlockAlign;
		short int BitsPerSample;
		char Sub_chunk2ID[4];
		int Sub_chunk2Size;
	};
public:
	static void createWavFile(std::vector<short> pcmData,int chunkSize,std::string& fileName)
	{
		WAVE_HEADER header;
		header.Chunk[0] = 'R';
		header.Chunk[1] = 'I';
		header.Chunk[2] = 'F';
		header.Chunk[3] = 'F';

		header.ChunkSize = chunkSize;

		header.format[0] = 'W';
		header.format[1] = 'A';
		header.format[2] = 'V';
		header.format[3] = 'E';

		header.Sub_chunk1ID[0] = 'f';
		header.Sub_chunk1ID[1] = 'm';
		header.Sub_chunk1ID[2] = 't';
		header.Sub_chunk1ID[3] = ' ';

		header.Sub_chunk1Size = 16;

		header.AudioFormat = 1;
		
		header.NumChannels = 2;

		header.SampleRate = GLOBAL::sampleRate;

		header.ByteRate = 192000;

		header.BlockAlign = 4;

		header.BitsPerSample = 16;

		header.Sub_chunk2ID[0] = 'd';
		header.Sub_chunk2ID[1] = 'a';
		header.Sub_chunk2ID[2] = 't';
		header.Sub_chunk2ID[3] = 'a';

		header.Sub_chunk2Size = 0;

		std::ofstream outFile(fileName, std::ios::binary);
		outFile.write(reinterpret_cast<char*>(&header), sizeof(header));
		outFile.write(reinterpret_cast<const char*>(pcmData.data()), pcmData.size() * sizeof(short));

		header.ChunkSize = pcmData.size() * sizeof(short) + sizeof(WAVE_HEADER) - 8;
		header.Sub_chunk2Size = pcmData.size() * sizeof(short);

		outFile.seekp(0, std::ios::beg);
		outFile.write(reinterpret_cast<char*>(&header), sizeof(header));

	}
	static void createWavFileMono(std::vector<short> pcmData, int chunkSize, std::string& fileName)
	{
		WAVE_HEADER header;
		header.Chunk[0] = 'R';
		header.Chunk[1] = 'I';
		header.Chunk[2] = 'F';
		header.Chunk[3] = 'F';

		header.ChunkSize = chunkSize;

		header.format[0] = 'W';
		header.format[1] = 'A';
		header.format[2] = 'V';
		header.format[3] = 'E';

		header.Sub_chunk1ID[0] = 'f';
		header.Sub_chunk1ID[1] = 'm';
		header.Sub_chunk1ID[2] = 't';
		header.Sub_chunk1ID[3] = ' ';

		header.Sub_chunk1Size = 16;

		header.AudioFormat = 1;

		header.NumChannels = 1;

		header.SampleRate = GLOBAL::sampleRate;

		header.ByteRate = 96000;

		header.BlockAlign = 4;

		header.BitsPerSample = 16;

		header.Sub_chunk2ID[0] = 'd';
		header.Sub_chunk2ID[1] = 'a';
		header.Sub_chunk2ID[2] = 't';
		header.Sub_chunk2ID[3] = 'a';

		header.Sub_chunk2Size = 0;

		std::ofstream outFile(fileName, std::ios::binary);
		outFile.write(reinterpret_cast<char*>(&header), sizeof(header));
		outFile.write(reinterpret_cast<const char*>(pcmData.data()), pcmData.size() * sizeof(short));

		header.ChunkSize = pcmData.size() * sizeof(short) + sizeof(WAVE_HEADER) - 8;
		header.Sub_chunk2Size = pcmData.size() * sizeof(short);

		outFile.seekp(0, std::ios::beg);
		outFile.write(reinterpret_cast<char*>(&header), sizeof(header));

	}
	static string getEnumString(Keys key);
	static string getEnumString(Key key);
	static vector<double> dX(vector<double> data);
	static void saveVectorToFile(const std::vector<double>& data, const std::string& filename);
	static vector<double> normalizeVector16(std::vector<short>& data, int bitDepth);
	static void createRawFile(vector<short> &data,const string &filename);
	static void createRawFile(vector<double>& data, const string& filename);
	static vector<short> doubleToShortScaled(const std::vector<double>& input);
	static vector<short> normalizeVector(std::vector<short>& data);
	static vector<double> integrate(vector<double> &data);
	static vector<int> noteSegmentation(vector<short>& left, vector<short>& right, vector<short> mono);


};

