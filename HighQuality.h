#pragma once
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <fstream>
#include <iostream>
#include <String>
using namespace std;

class HighQuality
{
public:

	string name;

	HighQuality(string fileName)
	{
		name = fileName;
	}


	void Init()
	{
		ifstream file(name, ios::binary);
		WAVE_HEADER header;
		file.read(reinterpret_cast<char*>(&header), sizeof(header));

		char* data = new char[header.Sub_chunk2Size];
		file.read(data, header.Sub_chunk2Size);
		file.close();

		std::cout << "Started WASAPI"<<endl;
		std::cout << "Size of samples " << data << endl;


		HRESULT res = CoInitialize(nullptr);

		IMMDeviceEnumerator* pEnumerator;
		CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);

		IMMDevice* pDevice;
		pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);

		IAudioClient* pAudioClient;
		pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClient);

		WAVEFORMATEX* pWaveFormat;
		pAudioClient->GetMixFormat(&pWaveFormat);

		pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, pWaveFormat, nullptr);

		IAudioRenderClient* pRenderClient;
		pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);

		UINT32 bufferFrameCount;
		pAudioClient->GetBufferSize(&bufferFrameCount);
		UINT32 numFrames = header.Sub_chunk2Size / (pWaveFormat->nChannels * (pWaveFormat->wBitsPerSample / 8));
		UINT32 numFramesWritten;
		pRenderClient->GetBuffer(bufferFrameCount, (BYTE**)&data);
		pRenderClient->ReleaseBuffer(numFrames, 0);

		//std::cout << "Starting playing audio using WASAPI"<<endl;

		pAudioClient->Start();
		
		Sleep((DWORD)(numFrames * 1000 / pWaveFormat->nSamplesPerSec));

		pAudioClient->Stop();
		

		pRenderClient->Release();
		pAudioClient->Release();
		pDevice->Release();
		pEnumerator->Release();

		// Uninitialize COM
		CoUninitialize();



	}
private:
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
};

