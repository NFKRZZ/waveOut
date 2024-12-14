#pragma once
#include <vector>

class NoteSegmentor
{
	std::vector<int> getNoteStart(std::vector<short> derivativeData);
	std::vector<int> onsetDetection(std::vector<short> pcmData);

};