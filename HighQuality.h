#pragma once
#include <propsys.h>
#include <functiondiscoverykeys.h>
#include <SetupAPI.h>
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <fstream>
#include <iostream>
#include <String>
#include <vector>
#include <chrono>


using namespace std;
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "setupapi.lib")

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
		cout << "STARTED INIT FOR WASAPI\n";
		HRESULT hr;
		ifstream file(name, ios::binary);
		if (!file.is_open()) 
		{
			cout << "Error opening file " << name << endl;
			return;
		}
		WAVE_HEADER header;
		file.read(reinterpret_cast<char*>(&header), sizeof(header));

		file.close();
		std::vector<short> data = getData(name);

		//Init COM
		hr = CoInitialize(nullptr);
		if (FAILED(hr))
		{
			cout << "Failed Initializing COM\n";
			return;
		}

		//Create IMMDeviceEnumerator to get all possible audio devices
		IMMDeviceEnumerator* pEnumerator = nullptr;
		hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
		if (FAILED(hr))
		{
			CoUninitialize();
			cout << "Failed to Create IMMDeviceEnumerator\n";
			return;
		}

		//Select which device to be used
		IMMDevice* pDevice = nullptr;
		hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
		if (FAILED(hr))
		{
			cout << "Failed to select audio device \n";
			pEnumerator->Release();
			CoUninitialize();
			return;
		}
		//get name of device
		LPWSTR pwszID = NULL;
		hr = pDevice->GetId(&pwszID);
		if (FAILED(hr))
		{
			cout << "Cannot Get Device Name\n";
		}

		IPropertyStore* pProps;
		hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
		if (FAILED(hr))
		{
			cout << "Couldnt get PropertyStore for string \n";
		}

		PROPVARIANT varName;
		PropVariantInit(&varName);
		hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
		if (FAILED(hr))
		{
			cout << "Couldnt get name of device \n";
		}

		std::wstring nameOfDevice(varName.pwszVal);

		cout << "This is the audio device using Friendlyname method selected: " << nameOfDevice.c_str() << endl;
		//Create render client for Audio

		IAudioClient* pAudioClient = nullptr;
		hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClient);
		if (FAILED(hr))
		{
			cout << "Failed to Activate the Audio Client for Render\n";
		}

		//Get the buffer
		UINT32 bufferFrames;
		hr = pAudioClient->GetBufferSize(&bufferFrames);
		if (FAILED(hr))
		{
			DWORD errorMessageID = GetLastError();
			LPSTR messageBuffer = nullptr;
			size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
			std::cout << "Failed to Get Buffer Size. Error message: " << messageBuffer << std::endl;
			LocalFree(messageBuffer);
		}

		//Get the render client for Audio
		IAudioRenderClient* pRenderClient = nullptr;
		hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);
		if (FAILED(hr))
		{
			cout << "Failed to Get Render Client\n";
		}

		//Start the Client
		hr = pAudioClient->Start();
		if (FAILED(hr))
		{
			cout << "FAILED TO START THE AUDIO CLIENT\n";
		}
		//Play Audio

		BYTE* pData;
		UINT32 numFramesPadding;
		hr = pRenderClient->GetBuffer(header.Sub_chunk2Size, &pData);
		if (FAILED(hr))
		{
			cout << "FAILED TO GET BUFFER FOR AUDIO\n";
		}

		memcpy(pData, &data[0], header.Sub_chunk2Size);
		hr = pRenderClient->ReleaseBuffer(header.Sub_chunk2Size, 0);
		if (FAILED(hr))
		{
			cout << "FAILED TO RELEASE THE BUFFER\n";
		}
		//Wait for buffer of audio to finish
		hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
		if (FAILED(hr))
		{
			cout << "Failed to get current padding\n";
		}

		while (numFramesPadding > 0)
		{
			Sleep((DWORD)(((float)header.Sub_chunk2Size) / header.ByteRate * 1000));
			hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
			if (FAILED(hr))
			{
				cout << "FAILED TO GET CURRENT PADDING\n";
			}
		}

		//Stop the audio client
		hr = pAudioClient->Stop();
		if (FAILED(hr))
		{
			cout << "Failed to stop audio client\n";
		}
		pRenderClient->Release();
		pAudioClient->Release();
		pEnumerator->Release();
		pDevice->Release();
		CoUninitialize();

	}
private:
	vector<short> getData(string file)
	{
		vector<short int> data;
		struct WAVE_HEADER waveheader;
		FILE* sound;
		bool foundList = false;
		errno_t err = fopen_s(&sound,file.c_str(), "rb");
		short int D;
		fread(&waveheader, sizeof(waveheader), 1, sound);

		//cout << "Chunk: " << waveheader.Chunk << endl;
		//cout << "Chunk Size: " << waveheader.ChunkSize << endl;
		//cout << "Format: " << waveheader.format << endl;

		//cout << "SubChunk1ID: " << waveheader.Sub_chunk1ID << endl;
		//cout << "SubChunk1Size: " << waveheader.Sub_chunk1Size << endl;
		//cout << "Audio Format: " << waveheader.AudioFormat << endl;
		//cout << "Num of Channels: " << waveheader.NumChannels << endl;
		//cout << "SampleRate: " << waveheader.SampleRate << endl;
		//cout << "ByteRate: " << waveheader.ByteRate << endl;
		//cout << "Block Align: " << waveheader.BlockAlign << endl;
		//cout << "Bits Per Sample:" << waveheader.BitsPerSample << endl;

		//cout << "SubChunk2ID: " << waveheader.Sub_chunk2ID << endl;
		//cout << "SubChunk2Size: " << waveheader.Sub_chunk2Size << endl;
		//cout << "sizeof " << sizeof(waveheader.BitsPerSample) << " reg " << waveheader.BitsPerSample << endl;

		wstring temp = wstring(file.begin(), file.end());
		LPCWSTR ws = temp.c_str();
		HANDLE h = CreateFile(ws, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);	//get Size
		int size = GetFileSize(h, NULL);

		char lol[4];
		int location = 0;
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
		cout << "Finished looking for LIST" << endl;
		int listSize = 0;
		int positon = 0;
		if (foundList)
		{
			read.seekg(location + 4);
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
		while (!feof(sound))
		{
			fread(&D, waveheader.BitsPerSample / 8, 1, sound);
			data.push_back(D);
		}
		std::chrono::system_clock::time_point now2 = std::chrono::system_clock::now();
		std::chrono::duration<double> elapsed = now2 - now1;
		cout << "Took " << elapsed.count() << " seconds" << endl;
		cout << "Finished populating vector" << endl;
		return data;
	}
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

