#pragma once
#include <iostream>
#include <vector>
#include "Chunk.h"
using namespace std;

class MidiMaker
{
public:
	MidiMaker()
	{

	}
	static vector<Chunk> lowPass(vector<short int> lowPassData);
	static vector<Chunk> bandPass(vector<short int> bandPassData);
	static vector<Chunk> highPass(vector<short int> highPassData);
private:

};

