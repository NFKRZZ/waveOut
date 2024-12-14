#include "NoteSegmentor.h"
#include "FUNCTIONS.h"
std::vector<int> NoteSegmentor::getNoteStart(std::vector<short> derivativeData)
{
	std::vector<short> temp(derivativeData.size());
	std::vector<int> noteStartIndices;
	for (int i = 0; i < derivativeData.size(); i++)
	{
		temp[i] = derivativeData[i] * derivativeData[i];
	}
	float sum = 0;
	float aver = 0;
	for (int j = 1; j < temp.size(); j++)
	{
		sum += temp[j];
		if (abs(aver - (sum / j)))
		{

		}

		aver = sum / j;

	}
	return noteStartIndices;
}

std::vector<int> NoteSegmentor::onsetDetection(std::vector<short> pcmData)
{
	std::vector<double> pcmDat = FUNCTIONS::short_to_double(pcmData);





	return std::vector<int>();
}

