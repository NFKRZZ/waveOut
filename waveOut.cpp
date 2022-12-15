#define _CRT_SECURE_NO_DEPRECATE
#include <windows.h>
#include <mmsystem.h>
#include <fstream>
#include <numeric>
#include <stdio.h>
#include <string>
#include <iostream>
#include<vector>
#include <algorithm>
#include <ctime>
#include <chrono>
#include "MiniBpm.h"
#include <fftw3.h>
#pragma comment(lib,"Winmm.lib")
using namespace std;

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
struct LIST
{
	char ChunkID[4];
	int ChunkSize;
	char ListTypeID[4];
};
static int BPM = 151;
static vector<short int> getData(string file)
{
	vector<short int> data;
	struct WAVE_HEADER waveheader;
	FILE* sound;
	bool foundList = false;
	sound = fopen(file.c_str(), "rb");
	short int D;
	fread(&waveheader, sizeof(waveheader), 1, sound);

	cout << "Chunk: " << waveheader.Chunk << endl;
	cout << "Chunk Size: " << waveheader.ChunkSize << endl;
	cout << "Format: " << waveheader.format << endl;

	cout << "SubChunk1ID: " << waveheader.Sub_chunk1ID << endl;
	cout << "SubChunk1Size: " << waveheader.Sub_chunk1Size << endl;
	cout << "Audio Format: " << waveheader.AudioFormat << endl;
	cout << "Num of Channels: " << waveheader.NumChannels << endl;
	cout << "SampleRate: " << waveheader.SampleRate << endl;
	cout << "ByteRate: " << waveheader.ByteRate << endl;
	cout << "Block Align: " << waveheader.BlockAlign << endl;
	cout << "Bits Per Sample:" << waveheader.BitsPerSample << endl;

	cout << "SubChunk2ID: " << waveheader.Sub_chunk2ID << endl;
	cout << "SubChunk2Size: " << waveheader.Sub_chunk2Size << endl;
	cout << "sizeof " << sizeof(waveheader.BitsPerSample) << " reg " << waveheader.BitsPerSample << endl;

	wstring temp = wstring(file.begin(), file.end());
	LPCWSTR ws = temp.c_str();
	HANDLE h = CreateFile(ws, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);	//get Size
	int size = GetFileSize(h,NULL);

	char lol[4];
	int location=0;
	ifstream read;
	read.open(file);
	read.seekg(0, read.beg);
	for (int i = 0;i < size;i++)
	{
		try
		{
			read.seekg(i);
			read.read(reinterpret_cast<char*>(&lol), sizeof(lol)); 
			if (lol[0] == 'L' && lol[1] == 'I' && lol[2] == 'S' && lol[3] == 'T')
			{
				location = i;
				foundList = true;
				cout << "FOUND LIST AT: " << i << endl;
				break;
			}
		}
		catch (exception e)
		{
		}
	}
	cout << "Finished looking for LIST"<<endl;
	int listSize=0;
	int positon = 0;
	if (foundList)
	{
		read.seekg(location+4);
		read.read(reinterpret_cast<char*>(&listSize), sizeof(listSize));
		cout << listSize << endl;
		read.seekg(location + 8 + listSize);
		positon = location + 8 + listSize;
	}
	else
	{
		positon = 44;
	}
	fseek(sound, positon, SEEK_SET);
	std::chrono::system_clock::time_point now1 = std::chrono::system_clock::now();
	/*read.seekg(0, read.beg);
	location = 0;
	for (int i = 0;i < size;i++)
	{
		try
		{
			read.seekg(i);
			read.read(reinterpret_cast<char*>(&lol), sizeof(lol));
			if (lol[0] == 'D' && lol[1] == 'A' && lol[2] == 'T' && lol[3] == 'A')
			{
				cout << "FOUND DATA at "<<i<<endl;
				location = i;
				break;
			}
		}
		catch (exception e)
		{
		}
	}
	cout << "Finished finding DATA" << endl;
	read.seekg(location + 4);
	int sizeofdata = 0;
	read.read(reinterpret_cast<char*>(&sizeofdata), 4);
	char* datas = (char*)&data[0];
	read.read(datas, sizeofdata);*/
	while (!feof(sound))
	{
		fread(&D, waveheader.BitsPerSample/8, 1, sound);
		data.push_back(D);
	}
	std::chrono::system_clock::time_point now2 = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed = now2 - now1;
	cout << "Took " << elapsed.count() << " seconds" << endl;
	cout << "Finished populating vector" << endl;
	return data;
}
void writeAudioBlock(HWAVEOUT hWaveOut, vector<short int> block, DWORD size)
{
	WAVEHDR header;
	ZeroMemory(&header, sizeof(header));
	header.dwBufferLength = size;
	header.lpData = (LPSTR)&block[0];

	waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
	waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));
	Sleep(500);
	while (waveOutUnprepareHeader(hWaveOut, &header, sizeof(WAVEHDR)) == WAVERR_STILLPLAYING)
	{
		Sleep(100);
	}
}
LPSTR loadAudioBlock(string filename, DWORD* blockSize)
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	DWORD size = 0;
	DWORD readBytes = 0;
	void* block = NULL;

	wstring temp = wstring(filename.begin(), filename.end());
	LPCWSTR ws = temp.c_str();

	if ((hFile=CreateFile(ws, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE)
	{
		return NULL;
	}

	do 
	{
		
		if ((size=GetFileSize(hFile, NULL)) == 0)
		{
			break;
		}
		cout <<"FILE SIZE RAW " << size << endl;
		if ((block=HeapAlloc(GetProcessHeap(), 0, size)) == NULL)
		{
			break;
		}
		ReadFile(hFile, block, size, &readBytes, NULL);
	} while (0);
	CloseHandle(hFile);
	*blockSize = size;
	return (LPSTR)block;

}
static WAVE_HEADER getHDR(string path)
{
	struct WAVE_HEADER hdr;
	FILE* l;
	l = fopen(path.c_str(), "rb");
	short D;
	fread(&hdr, sizeof(hdr), 1, l);
	return hdr;
}
pair<vector<short int>, vector<short int>> LeftRight(vector<short int> origin)
{
	cout << "Size of samples:" << origin.size() << endl;
	vector<short int> left;
	vector<short int> right;
	int size = origin.size();
	for (int i = 0;i < size;i++)
	{
		if (i % 2 == 0)
		{
			left.push_back(origin[i]);
		}
		else
		{
			right.push_back(origin[i]);
		}
	}
	cout << "Size of left: " << left.size()<<endl;
	cout << "Size of right: " << right.size()<<endl;
	return make_pair(left, right);
}
vector<short int> Consolidate(vector<short int> left,vector<short int> right)
{
	vector<short int> consolidated;
	int size = (left.size() + right.size())/2;

	for (int i = 0;i < size;i++)
	{
		consolidated.push_back((left[i] + right[i]) / 2);
	}
	cout <<"SIZE "<< consolidated.size() << endl;
	return consolidated;
}
int main(int argc, char* argv[])
{
	string file = "151bpm_chords_2semitonedown_hsSynth.wav";
	cout << file << endl;
	HWAVEOUT hWaveOut;
	LPSTR block;
	DWORD blockSize;
	WAVE_HEADER wav = getHDR(file);
	WAVEFORMATEX format = 
	{
		WAVE_FORMAT_PCM,	//FORMAT
		2,					//CHANNELS
		wav.SampleRate/1.0,	    //SAMPLE RATE
		wav.ByteRate,		//AVG BYTES PER SEC
		wav.BlockAlign,		//BLOCK ALIGN
		wav.BitsPerSample,	//BITS PER SAMPLE
		0					//CBSIZE
	};
	MMRESULT m;
	if ((m=waveOutOpen(&hWaveOut, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL)) != MMSYSERR_NOERROR)
	{
		printf("ERROR");
		cout << m << endl;
		ExitProcess(0);
	}
	printf("The Wave Mapper devicce was loaded\n");
	vector<short int> pcmData = getData(file);
	int pcmSize = sizeof(short int) * pcmData.size();
	cout << "PCMDATA SIZE  " << pcmSize <<endl;
	/*if ((block = loadAudioBlock("someone.raw", &blockSize)) == NULL)//get from wav
	{
		printf("Cant load file\n");
		ExitProcess(1);
	}*/
	blockSize = pcmSize;

	printf("LOADED BLOCK\n");
	//breakfastquay::MiniBPM bpm(wav.SampleRate); // Must consolidate samples for both samples into 1 and divide by 2 to keep regular amplitude
	//double bp = bpm.estimateTempoOfSamples((float*)&pcmData[0], pcmData.size());
	//cout << "BPM: " << bp;
	writeAudioBlock(hWaveOut, pcmData, blockSize);
	waveOutClose(hWaveOut);
	pair<vector<short int>, vector<short int>> dat = LeftRight(pcmData);
	vector<short int> preProcData = Consolidate(dat.first, dat.second);
	vector<double> audiodata(preProcData.begin(), preProcData.end());
	cout << "BPM: " << BPM << endl;
	float qBeatDuration = (1.0 / (BPM / 60.0)) / 4.0;
	cout << qBeatDuration<<endl;
	int sampleSize = qBeatDuration * wav.SampleRate/2;
	int numOfChunks = audiodata.size() / sampleSize;
	cout << sampleSize << endl;
	vector<vector<double>> sampleChunks;
	int inputSize = 2048;//4096 wont work
	int outputSize = (inputSize / 2) + 1;
	int flags = FFTW_ESTIMATE;
	cout << "HELLO " <<numOfChunks <<endl;
	cout << audiodata[0] << endl;
	sampleChunks.resize(numOfChunks);
	int N = 10;
	for (int i = 0;i < numOfChunks;i++)
	{
		for (int j = 0;j < sampleSize;j++)
		{
			//cout << i << " " << j << endl;
			//cout << audiodata[(i * sampleSize) + j] << endl;
			sampleChunks[i].push_back(audiodata[(i*sampleSize)+j]);
		}
		// DO FFT get back n Frequencies
		// 2D array ----> [ [0,FREQENCIES],[1,FREQUENCIES]   ]
		// ITERATE THROUGH ALL SAMPLES
		
		//then the top N amount of frequencies will be logged
		// this will be stored in an object that has the following member variables: a pair vector with frequency and intensity, 
		// also put in the object is the chunk iteration or corresponding time range
		// there will be a vector of these objects

		//then we will attempt to create a midi file from this 
	}
	//cout << "did it" << endl;
	for (int i = 0;i < numOfChunks;i++)
	{
		vector<double>* pointer = &sampleChunks[i];
		fftw_complex* output_buffer = static_cast<fftw_complex*>(fftw_malloc(outputSize * sizeof(fftw_complex)));
		fftw_plan plan = fftw_plan_dft_r2c_1d(inputSize,&sampleChunks[i][0] , output_buffer, flags);
		fftw_execute(plan);
		//cout << "yess" <<endl;
		vector<double> test;
		for (int i = 0; i < outputSize - 1;i++)
		{
			test.push_back((double)output_buffer[i][0]);
			//cout <<"Frequency:"<<(48000/inputSize)*i<<" Hz "<<" Intensity: " << (double)output_buffer[i][0] << endl;
		}
		auto lol = max_element(test.begin(), test.end());
		cout << i << " CHUNK: " << " " << audiodata[numOfChunks] << " " << numOfChunks << "  Largest frequency is " << distance(begin(test), lol) * (48000 / inputSize) << endl;

		//get nth top frequencies
		vector<size_t> index(test.size());
		iota(test.begin(), test.end(), 0);
		std::partial_sort(index.begin(), index.begin() + N, index.end(),
			[&](size_t A, size_t B) {
			return test[A] > test[B];
		});

	}



	cout << "Finished";


	//WRITE TO MIDI

	return 0;
}
