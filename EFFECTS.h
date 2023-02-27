#include <vector>
#include <string>
class EFFECTS
{
public:
	static std::vector<short int> SIDECHAIN(std::vector<short int> audioData, double BPM, int sampleRate,std::string METHOD); //applies a sidechain to audio/ truncates values according to a periodic function

	static void PHASER(std::vector<short int> audioData, double RAD); //applies a phaser to the audio with a specified phase shift in radians

	static std::pair<std::vector<short int>, std::vector<short int>> REVERB(const std::vector<short int>& leftChannel, const std::vector<short int>& rightChannel, int sampleRate, int delayInMilliseconds, float decay);

	static std::pair<std::vector<short int>, std::vector<short int>> DELAY(const std::vector<short>& left,const std::vector<short>& right,double delayInMilliseconds,float decay,int sampleRate);
};

