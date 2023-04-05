#pragma once

#include <vector>
class Chunk
{
public:
	Chunk(std::pair<std::vector<double>,std::vector<double>> FrequencyIntensity, int iteration, double secondInit, double secondEnd)
	{
		freqInten = FrequencyIntensity;
		iter = iteration;
		start = secondInit;
		end = secondEnd;
	}
private:
	std::pair<std::vector<double>, std::vector<double>> freqInten;
	int iter;
	double start;
	double end;


};

