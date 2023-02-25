#include "EFFECTS.h"
#include<vector>
#include <iostream>
#include <string>

std::vector<short int> EFFECTS::SIDECHAIN(std::vector<short int> audioData, double BPM, int SampleRate,std::string METHOD)
{
	std::cout << "SIZE OF AUDIODATA " << audioData.size() << std::endl;
	float qBeatDuration = (1.0 / (BPM / 60.0)) / 1.0; 
	int sampleSize = qBeatDuration * (SampleRate / 2);
	// from x = 0 to x = 0.786
	//sin(x+0.786)*cos(x+0.786)+0.5/tan(x+0.786)
	std::vector<double> weights(sampleSize);
	std::vector<double> xVal(sampleSize);
	for (int i = 0;i < sampleSize;i++)
	{
		xVal[i] = 0.786 * i / (sampleSize - 1);
	}

	for (int i = 0;i < sampleSize;i++)
	{
		weights[i] = (sin(xVal[i] + 0.786) * cos(xVal[i] + 0.786) + 0.5) / tan(xVal[i] + 0.78); //make function more fluid transisitions are jarring
		//weights[i] = sin(xVal[i]);
	}
	//Periodic Weight function determined

	//Data divided up so periodic weight can be applied
	int numOfChunks = audioData.size() / (sampleSize);
	std::vector<short int> procData;
	for (int i = 0;i < numOfChunks;i++)
	{
		for (int j = 0;j < sampleSize;j++)
		{
			procData.push_back(weights[j]*audioData[(i * sampleSize) + j]);
		}
	}
	int paddingSize = audioData.size() - procData.size();
	for (int i = 0; i < paddingSize; i++) {
		procData.push_back(0);
	}
	std::cout <<"SIZE OF SIDECHAIN " << procData.size() << std::endl;
	return procData;
}

void EFFECTS::PHASER(std::vector<short int> audioData, double RAD)
{

}

void EFFECTS::REVERB(std::vector<short int> audioData, double WET)
{

}



