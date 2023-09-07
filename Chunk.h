#pragma once

#include <vector>
#include "Keys.h"
#include <string>
#include "GLOBAL.h"
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

	Chunk(std::vector<double> freq,std::vector<double> inten,int iter,float chunkDuration)
	{
		freqVec = freq;
		intenVec = inten;
		singular = false;
		this->iter = iter;
		this->chunkDuration = chunkDuration;
	}


	int getFreq()
	{
		return freq;
	}

	std::vector<double> getFreqV()
	{
		return freqVec;
	}

	std::vector<Keys> getKeyVec()
	{
		return keyVec;
	}

	Keys getKey()
	{
		return key;
	}
	
	int getIter()
	{
		return iter;
	}

	std::vector<double> getIntenVec()
	{
		return intenVec;
	}


	std::string getStartTime()
	{

		std::string minutesStr = (startMinute < 10) ? "0" + std::to_string(startMinute) : std::to_string(startMinute);
		std::string secondsStr = (startSecond < 10) ? "0" + std::to_string(startSecond) : std::to_string(startSecond);
		std::string millisecondsStr = (startMili < 10) ? "00" + std::to_string(startMili) : ((startMili < 100) ? "0" + std::to_string(startMili) : std::to_string(startMili));

		return minutesStr + ":" + secondsStr + ":" + millisecondsStr;
	}

	std::string getEndTime()
	{

		std::string minutesStr = (endMinute < 10) ? "0" + std::to_string(endMinute) : std::to_string(endMinute);
		std::string secondsStr = (endSecond < 10) ? "0" + std::to_string(endSecond) : std::to_string(endSecond);
		std::string millisecondsStr = (endMili < 10) ? "00" + std::to_string(endMili) : ((endMili < 100) ? "0" + std::to_string(endMili) : std::to_string(endMili));

		return minutesStr + ":" + secondsStr + ":" + millisecondsStr;
	}
	float getStart()
	{
		return this->chunkDuration * this->iter;
	}

	float getEnd()
	{
		if (chunkDuration * (iter + 1) > GLOBAL::SONG_LENGTH)
		{
			return GLOBAL::SONG_LENGTH;
		}
		else 
		{
			return this->chunkDuration * (iter + 1);
		}
	}

	
	void Init();


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

	int startMinute;
	int startSecond;
	int startMili;

	int endMinute;
	int endSecond;
	int endMili;

	float chunkDuration;

	void setTime();
	void clampKeys(); //This function takes the raw frequency data and then clamps it to the closest appropriate note as determined by the musical Key the song is in
};

