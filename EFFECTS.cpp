#include "EFFECTS.h"
#include<vector>
#include <iostream>
#include <string>

std::vector<short int> EFFECTS::SIDECHAIN(std::vector<short int> audioData, double BPM, int SampleRate,std::string METHOD)//FIXED 1/4 PERIODIC
{

	float qBeatDuration = (1.0 / (BPM / 60.0)) / 0.5; 
	int sampleSize = qBeatDuration * (SampleRate / 2); //ASSUMMING THAT THIS IS ONLY ONE SIDE OF STEREO AUDIO IF IT IS BOTH SIDES THEN REMOVE /2
	std::vector<double> weights(sampleSize);
	std::vector<double> xVal(sampleSize);


	if (strcmp(METHOD.c_str(), "TAN") == 0)
	{
		for (int i = 0;i < sampleSize;i++)
		{
			xVal[i] = 0.786 * i / (sampleSize - 1);
		}
		for (int i = 0;i < sampleSize;i++)
		{
			weights[i] = (sin(xVal[i] + 0.786) * cos(xVal[i] + 0.786) + 0.5) / tan(xVal[i] + 0.78); //make function more fluid transisitions are jarring

		}
	}
	else if (strcmp(METHOD.c_str(), "ARCTAN") == 0)
	{
		for (int i = 0;i < sampleSize;i++)
		{
			xVal[i] = 6.2831 * i / (sampleSize - 1);
		}
		for (int i = 0;i < sampleSize;i++)
		{
			weights[i] = ((atan(xVal[i] - 3.14159286) / 1.3) + 1) / 2;
		}
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
	for (int i = 0; i < paddingSize; i++) 
	{
		procData.push_back(0);
	}
	return procData;
}

void EFFECTS::PHASER(std::vector<short int> audioData, double RAD)
{

}



std::pair<std::vector<short int>, std::vector<short int>> EFFECTS::REVERB(const std::vector<short int>& leftChannel, const std::vector<short int>& rightChannel, int sampleRate, int delayInMilliseconds, float decay)
{
	// Compute the delay in samples based on the sample rate
	int delayInSamples = static_cast<int>((delayInMilliseconds / 1000.0) * sampleRate);

	// Create the delay buffer
	std::vector<std::pair<short int, short int>> delayBuffer(delayInSamples, std::make_pair(0, 0));

	// Create the output vectors
	std::vector<short int> outputLeftChannel(leftChannel.size());
	std::vector<short int> outputRightChannel(rightChannel.size());

	// Apply reverb to each input sample
	for (size_t i = 0; i < leftChannel.size(); ++i) {
		// Compute the delayed sample for the left channel
		int delayIndex = static_cast<int>(i % delayInSamples);
		std::pair<short int, short int> delayedSampleLeft = delayBuffer[delayIndex];

		// Compute the delayed sample for the right channel
		delayIndex = static_cast<int>((i + delayInSamples / 2) % delayInSamples);
		std::pair<short int, short int> delayedSampleRight = delayBuffer[delayIndex];

		// Compute the reverb sample for the left channel
		short int reverbSampleLeft = static_cast<short int>((leftChannel[i] + delayedSampleLeft.first) * decay);

		// Compute the reverb sample for the right channel
		short int reverbSampleRight = static_cast<short int>((rightChannel[i] + delayedSampleRight.second) * decay);

		// Update the delay buffer with the current input sample
		delayBuffer[delayIndex] = std::make_pair(reverbSampleLeft, reverbSampleRight);

		// Update the output vectors with the current sample
		outputLeftChannel[i] = reverbSampleLeft;
		outputRightChannel[i] = reverbSampleRight;
	}

	// Return the output channels as a pair
	return std::make_pair(outputLeftChannel, outputRightChannel);
}

std::pair<std::vector<short int>, std::vector<short int>> EFFECTS::DELAY(const std::vector<short>& left,const std::vector<short>& right,double delayInMilliseconds,float decay,int sampleRate)
{

	int delayInSamples = (delayInMilliseconds * sampleRate) / 1000;
	std::vector<short> leftDelayed(left.size() + delayInSamples, 0);
	std::vector<short> rightDelayed(right.size() + delayInSamples, 0);

	for (int i = delayInSamples; i < leftDelayed.size(); i++) {
		leftDelayed[i] = left[i - delayInSamples] + decay * leftDelayed[i - delayInSamples];
		rightDelayed[i] = right[i - delayInSamples] + decay * rightDelayed[i - delayInSamples];

		// Clip the sample if it exceeds the maximum value
		if (leftDelayed[i] > SHRT_MAX) {
			leftDelayed[i] = SHRT_MAX;
			std::cout << "max exceeded " << i << std::endl;
		}
		else if (leftDelayed[i] < -SHRT_MAX) {
			leftDelayed[i] = -SHRT_MAX;
			std::cout << "max exceeded " << i << std::endl;
		}

		if (rightDelayed[i] > SHRT_MAX) {
			rightDelayed[i] = SHRT_MAX;
			std::cout << "max exceeded " << i << std::endl;
		}
		else if (rightDelayed[i] < -SHRT_MAX) {
			rightDelayed[i] = -SHRT_MAX;
			std::cout << "max exceeded " << i << std::endl;
		}
	}

	return std::make_pair(leftDelayed, rightDelayed);
}



