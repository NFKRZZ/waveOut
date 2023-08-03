#pragma once

#include <vector>
#include "Keys.h"
class Chunk
{
public:
	Chunk(std::pair<std::vector<double>,std::vector<double>> FrequencyIntensity, int iteration, double secondInit, double secondEnd)
	{
		freqInten = FrequencyIntensity;
		iter = iteration;
		start = secondInit;
		end = secondEnd;
		singular = false;
	}
	Chunk(double freq, double inten, int interation, double secondInit,double secondEnd)
	{
		singular = true;
		this->freq = freq;
		this->inten = inten;
	}

	Chunk(std::vector<double> freq,std::vector<double> inten,int iter)
	{
		freqVec = freq;
		intenVec = inten;
		singular = false;
	}


	int getFreq()
	{
		return freq;
	}

	std::vector<double> getFreqV()
	{
		return freqVec;
	}

	void clampKeys(); //This function takes the raw frequency data and then clamps it to the closest appropriate note as determined by the musical Key the song is in

private:
	bool singular;
	std::pair<std::vector<double>, std::vector<double>> freqInten;
	double freq;
	double inten;
	int iter;
	double start;
	double end;
	std::vector<double> freqVec;
	std::vector<double> intenVec;
	std::vector<Keys> keyVec;
	Keys key;

};

