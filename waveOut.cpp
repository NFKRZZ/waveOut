#define _CRT_SECURE_NO_WARNINGS
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
#include <array>
#include "MiniBpm.h"
#include <fftw3.h>
#include "filter.cpp"
#include "Keys.h"
#include "EFFECTS.h"
#include "Chunk.h"
#include "Util.h"
#include "HighQuality.h"
#include "GLOBAL.h"
#include "MidiMaker.h"
#include "KeyDetection.h"
#include <iomanip>
#include "StemSeperator.h"
#include <filesystem>

#include <keyfinder/keyfinder.h>

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

	MMRESULT jk = waveOutPrepareHeader(hWaveOut, &header, sizeof(WAVEHDR));
	cout << "JK IS " << jk << endl;
	MMRESULT lol = waveOutWrite(hWaveOut, &header, sizeof(WAVEHDR));
	cout << "lol is " << lol << endl;
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
vector<short> Stereoize(vector<short> left, vector<short> right)
{
	size_t num_samples = min(left.size(), right.size());

	std::vector<short> interleaved(num_samples * 2);

	for (size_t i = 0; i < num_samples; i++) {
		interleaved[2 * i] = left[i];
		interleaved[2 * i + 1] = right[i];
	}

	return interleaved;
}


int main(int argc, char* argv[])
{
	
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE);

	std::cout << " ________  ___  ___  ________  ___  ________          _________  ________          _____ ______   ___  ________  ___     \n";
	std::cout << "|\\   __  \\|\\  \\|\\  \\|\\   ___ \\|\\  \\|\\   __  \\        |\\___   ___\\\\   __  \\        |\\   _ \\  _   \\|\\  \\|\\   ___ \\|\\  \\    \n";
	std::cout << "\\ \\  \\|\\  \\ \\  \\\\\\  \\ \\  \\_|\\ \\ \\  \\ \\  \\|\\  \\       \\|___ \\  \\_\\ \\  \\|\\  \\       \\ \\  \\\\\\__\\ \\  \\ \\  \\ \\   \\_|\ \\ \\  \\   \n";
	std::cout << " \\ \\   __  \\ \\  \\\\\\  \\ \\  \\ \\\\ \\ \\  \\ \\  \\\\\\  \\           \\ \\  \\ \\ \\  \\\\\   \\       \\ \\  \\\\|__| \\  \\ \\  \\ \\  \\  \\ \\ \\  \\ \n";
	std::cout << "  \\ \\  \\ \\  \\ \\  \\\\\\  \\ \\  \\_\\\\ \\ \\  \\ \\  \\\\\\  \\           \\ \\  \\ \\ \\  \\\\\   \\       \\ \\  \\    \\ \\  \\ \\  \\ \\  \\_\\  \\ \\  \\ \n";
	std::cout << "   \\ \\__\\ \\__\\ \\_______\\ \\_______\\ \\__\\ \\_______\\           \\ \\__\\ \\ \\_______\\       \\ \\__\\    \\ \\__\\ \\__\\ \\_______\\ \\__\\\n";
	std::cout << "    \\|__|\\|__|\\|_______|\\|_______|\\|__|\\|_______|            \\|__|  \\|_______|        \\|__|     \\|__|\\|__|\\|_______|\\|__|\n";
	SetConsoleTextAttribute(hConsole,7);

	std::chrono::system_clock::time_point now1 = std::chrono::system_clock::now();
    //C:/Users/winga/Music
	string file = "Test/distance2.wav";
	std::filesystem::path p(file);
	string filename = p.stem().string();

	std::cout << "Running Stem Seperation with CUDA!" << endl;
	StemSeperator::split(file);
	std::cout << "Finished!" << endl;
	cout << filename << endl;
	//path of stems is ../seperated/htdemucs_ft/file_name
	// bass drums other vocals
	string bass = "separated/htdemucs_ft/"+ filename + "/bass.wav";
	string vocals = "separated/htdemucs_ft/"+filename+"/vocals.wav";
	string drums = "separated/htdemucs_ft/"+filename+"/drums.wav";
	string other = "separated/htdemucs_ft/"+filename+"/other.wav";


	//lets load these files

	vector<short int> bassData = getData(bass);
	vector<short int> vocalData = getData(vocals);
	vector<short int> drumData = getData(drums);
	vector<short int> chordData = getData(other);
	cout << "Loaded stems into memory" << endl;
	Key SONG_KEY = Key::NO_KEY;
	GLOBAL::MUSICAL_KEY = SONG_KEY;
	GLOBAL::isMonophonic = true; //WORK ON THIS NEXT ------------------------------------------------------------------------------------------ L()()K
	cout << file << endl;
	HWAVEOUT hWaveOut;
	LPSTR block;
	DWORD blockSize;
	WAVE_HEADER wav = getHDR(file);
	WAVEFORMATEX format = 
	{
		WAVE_FORMAT_PCM,	//FORMAT
		2,					//CHANNELS
		wav.SampleRate/1.0,	//SAMPLE RATE
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
	blockSize = pcmSize;
	printf("LOADED BLOCK\n");
	
	




	vector<long double> coefficients = filter::yLcalculate_high_pass_filter_coefficients(wav.SampleRate,1000,1000 ); //crappy filter or coefficients after like 500 hz there is crackling; 550hz only one instance of fucked up audio
	//                                                                                                      600hz kicks up the shit audio & 700hz nails the coffin, num_taps does not fix this at all // reason gives
	pair<vector<short>, vector<short>> dat1 = LeftRight(pcmData);                                           // cracks even at 500 hz at kicks
	vector<short> left = dat1.first;
	vector<short> right = dat1.second;

	vector<double> leftD = filter::short_to_double(left);
	vector<double> rightD = filter::short_to_double(right);


	//get Song Key and BPM
	
	vector<short int> mono = Consolidate(dat1.first, dat1.second);
	vector<double> monoD = filter::short_to_double(mono);
	
	KeyFinder::KeyFinder kf;

	Key k = KeyDetection::getKey(monoD, wav.SampleRate, kf);

	string key = Util::getEnumString(k);



	cout << "BPM: " << BPM << endl;
	cout << "Song Key: " << Util::getEnumString(k) << endl;
	




	
	cout << "Finished oh yea \n";
	vector<short int> data = Stereoize(left, right);
	cout<<"Max Value is : " << *max_element(data.begin(), data.end()) << endl;
	
	//writeAudioBlock(hWaveOut, data, blockSize);
	//waveOutClose(hWaveOut);

	//HighQuality l("Test/nightdrive.wav");
	//l.Init();

	int cuttoff_f = 200;
	vector<short> leftLowPass = dat1.first;
	vector<short> rightLowPass = dat1.second;
	filter::lowPassFFTW_HannWindow(leftLowPass, rightLowPass, wav.SampleRate, cuttoff_f);
	vector<short int> lowPassDat = Stereoize(leftLowPass, rightLowPass);


	leftLowPass.clear();
	rightLowPass.clear();
	leftLowPass.shrink_to_fit();
	rightLowPass.shrink_to_fit();


	vector<short> leftHighPass = dat1.first;
	vector<short> rightHighPass = dat1.second;
	filter::highPassFFTW(leftHighPass, rightHighPass, wav.SampleRate, cuttoff_f);
	vector<short int> highPassDat = Stereoize(leftHighPass, rightHighPass);

	leftHighPass.clear();
	rightHighPass.clear();
	leftHighPass.shrink_to_fit();
	rightHighPass.shrink_to_fit();



	/////////////////////////LOW PASS CONVOLUTION////////////////////////////

	vector<short> properL = dat1.first;
	vector<short> properR = dat1.second;
	filter::yLapply_high_pass_filter(properL, properR, coefficients);
	vector<short> convolutionData = Stereoize(properL, properR);

	//////////////////////////////////////////////////////////////////////////

	/////////////////////EFFECT SIDECHAIN////////////////////////////////////


	float twoBeatDuration = (1 / (BPM / 60.0)) / 0.5;
	float qBeatDuration = (1.0 / (BPM / 60.0)) / 4.0;
	
	GLOBAL::qBeatDuration = qBeatDuration;
	GLOBAL::sampleRate = wav.SampleRate;
	GLOBAL::twoBeatDuration = twoBeatDuration;

	int sampleSizeQuart = qBeatDuration * wav.SampleRate;
	int sampleSizeTwo = twoBeatDuration * wav.SampleRate;
	//LOW PASS HAS MORE SAMPLES FOR FINER RES; HIGH PASS HAS LESS SAMPLES CUZ LESS RES NEEDED
	int numOfChunkQuart = highPassDat.size() / sampleSizeQuart;
	int numOfChunkTwo =	lowPassDat.size() / sampleSizeTwo;

	vector<double> lowPassDatDouble(lowPassDat.begin(), lowPassDat.end());
	vector<double> highPassDatDouble(highPassDat.begin(), highPassDat.end());

	vector<vector<double>> sampleChunkQuart;
	vector<vector<double>> sampleChunkTwo;








	pair<vector<short int>, vector<short int>> dat = LeftRight(data);//use pcmData for unfiltered fft, data for filtered fft
	vector<short int> preProcData = Consolidate(dat.first, dat.second);
	vector<double> audiodata(preProcData.begin(), preProcData.end());
	cout << "BPM: " << BPM << endl;
	cout << qBeatDuration<<endl;
	int sampleSize = qBeatDuration * (wav.SampleRate/2);
	int numOfChunks = audiodata.size() /( sampleSize*2);
	cout <<"THIS IS SAMPLESIZE MAIN FUNC " << sampleSize << endl;
	cout << "Length of Audio is " << numOfChunks * qBeatDuration << " seconds \n";
	GLOBAL::SONG_LENGTH = numOfChunks * qBeatDuration;
	vector<vector<double>> sampleChunks;
	int inputSize = 1024;//4096 wont work; possible error in how the output data is being stored
	int outputSize = (inputSize / 2) + 1;
	int flags = FFTW_ESTIMATE;
	cout << "HELLO THIS IS NUMOFCHUNKS MAIN FUNC " <<numOfChunks <<endl;
	cout <<"THIS IS AUDIODATA SIZE MAIN FUNC " << audiodata.size() << endl;

	cout << "THIS IS qBEAT MAIN " << std::setprecision(15) <<qBeatDuration << endl;

	sampleChunks.resize(numOfChunks);
	int N = 10;

	vector<Chunk> chunkData;
	cout << "starting chunk haha" << endl;
	pair<vector<short int>, vector<short int>> lowP = LeftRight(lowPassDat);
	pair<vector<short int>, vector<short int>> highP = LeftRight(highPassDat);


	cout << "THIS IS LOWPASS DAT SIZE: " << lowPassDat.size() << endl;


	vector<short int> lowPP = Consolidate(lowP.first, lowP.second);
	vector<short int> highPP = Consolidate(highP.first, highP.second);
	cout << "THIS IS LOWPP SIZE: " << lowPP.size()<<endl;
	vector<Chunk> cDat = MidiMaker::lowPass(lowPP);
	cout << "DID LOW PASS\n";
	vector<Chunk> midPass = MidiMaker::highPass(highPP); //I THINK CHUNKS ARENT BEING DONE PROPERLY TIMING IS WRONG

	std::cout << "This is chunk seperation time: " << GLOBAL::twoBeatDuration << "s" << endl;
	for (Chunk c : cDat)
	{
		vector<Keys> p = c.getKeyVec();
		vector<double> inten = c.getIntenVec();
		for (int i = 0;i<p.size();i++)
		{
			cout << "Low Pass iteration: "<< c.getIter()<<" TIME: "<<c.getStart() <<" to "<<c.getEnd() <<" Intensity: "<< 20*log(inten[i]/32768) << " Key: " << Util::getEnumString(p[i]) << endl;
		}
	}
	cout << endl;
	for (Chunk q : midPass)
	{
		vector<Keys> p = q.getKeyVec();
		vector<double> inten = q.getIntenVec();
		vector<double> freqvec = q.getFreqV();
		for (int i = 0;i<p.size();i++)
		{
			cout << "High Pass iteration: " << q.getIter() <<" TIME: "<<q.getStart()<<" to "<<q.getEnd() <<" Frequency: "<<freqvec[i]<<" Hz " << " Intensity: " << 20 * log(inten[i] / 32768) << " Key: " << Util::getEnumString(p[i]) << endl;
		}
	}








	for (int i = 0;i < numOfChunks;i++)
	{
		for (int j = 0;j < sampleSize;j++)
		{
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
	for (int i = 0;i < numOfChunks;i++)
	{
		vector<double>* pointer = &sampleChunks[i];
		fftw_complex* output_buffer = static_cast<fftw_complex*>(fftw_malloc(outputSize * sizeof(fftw_complex)));
		fftw_plan plan = fftw_plan_dft_r2c_1d(inputSize,&sampleChunks[i][0] , output_buffer, flags);
		fftw_execute(plan);
		vector<double> test;
		for (int i = 0; i < outputSize - 1;i++)
		{
			test.push_back((double)output_buffer[i][0]);
		}
		auto lol = max_element(test.begin(), test.end());
		//cout << i << " CHUNK: " << " Time: "<< qBeatDuration*(i+1) << " " << numOfChunks << "  Largest frequency is " << distance(begin(test), lol) * (wav.SampleRate / inputSize) << endl;

		double frequency = distance(begin(test), lol) * (wav.SampleRate / inputSize);
		vector<double> freqVector = { frequency };
		vector<double> intenVector = { 1.0 };

		Chunk chunk = Chunk(make_pair(freqVector,intenVector), i, qBeatDuration * (i + 1), qBeatDuration * (i + 2));
		chunkData.push_back(chunk);
		//get nth top frequencies
		vector<size_t> index(test.size());
		iota(test.begin(), test.end(), 0);
		std::partial_sort(index.begin(), index.begin() + N, index.end(),
			[&](size_t A, size_t B) {
			return test[A] > test[B];
		});

	}

	cout << "Length of ChunkData " << chunkData.size() << " \n";
	
	cout << "Finished\n";


	//WRITE TO MIDI



	//create Wav
	string fName = "data.wav";
	string lola = "high.wav";
	string smar = "proper.wav";
	string aja = "bigboi.wav";
	string ooo = "jaj.wav";
	string sid = "delay.wav";
	Util::createWavFile(lowPassDat,wav.ChunkSize,fName);
	Util::createWavFile(highPassDat, wav.ChunkSize, lola);
	Util::createWavFile(convolutionData, wav.ChunkSize, smar);
	cout << "Convolution Data Size: " << convolutionData.size() << " wav.Chunksize: " << wav.ChunkSize << endl;
	//MidiMaker::doSomething();
	vector<double> j = Util::normalizeVector16(preProcData, 16);
	j[0] = 0;
	j[1] = 0;
	Util::createRawFile(preProcData, "jojo.raw");
	Util::createRawFile(j, "hahaha.raw");
	vector<double> deriv = Util::dX(j);
	Util::createRawFile(deriv, "dderiv.raw");

	std::cout << "AHAHAHA" << endl;
	vector<double> integ = Util::integrate(j);
	vector<short> integSc = Util::doubleToShortScaled(integ);
	Util::createWavFileMono(Util::normalizeVector(integSc), wav.ChunkSize, ooo);
	std::cout << "oha" << endl;

	Util::saveVectorToFile(deriv, "hello.txt");
	std::chrono::system_clock::time_point now2 = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed = now2 - now1;
	cout << "Took " << elapsed.count() << " seconds" << endl;
	vector<short>scaled = Util::doubleToShortScaled(deriv);
	//Util::noteSegmentation(left, right, scaled);
	Util::createWavFileMono(Util::normalizeVector(scaled),wav.ChunkSize, aja);
	

	return 0;
}
