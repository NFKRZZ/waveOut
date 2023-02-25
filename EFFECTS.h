#include <vector>
class EFFECTS
{
public:
	static std::vector<short int> SIDECHAIN(std::vector<short int> audioData, double BPM, int sampleRate); //applies a sidechain to audio/ truncates values according to a periodic function

	void PHASER(std::vector<short int> audioData, double RAD); //applies a phaser to the audio with a specified phase shift in radians
	
	void REVERB(std::vector<short int> audioData, double WET);
};

