#pragma once

#include <vector>
#include <string>
#include <utility>

class GLOBAL
{
public:
	static float qBeatDuration;
    static int sampleRate;
	static float twoBeatDuration;
	static Key MUSICAL_KEY;
	static bool isMonophonic;
	static int chordVoices;
	static std::vector<Keys> cMajor;
	static std::vector<Keys> cSharpMajor;
	static std::vector<Keys> dMajor;
	static std::vector<Keys> dSharpMajor;
	static std::vector<Keys> eMajor;
	static std::vector<Keys> fMajor;
	static std::vector<Keys> fSharpMajor;
	static std::vector<Keys> gMajor;
	static std::vector<Keys> gSharpMajor;
	static std::vector<Keys> aMajor;
	static std::vector<Keys> aSharpMajor;
	static std::vector<Keys> bMajor;
	
	static std::vector<std::pair<std::string, int>> keysString;

	static bool Init();



};