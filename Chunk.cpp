#include "Chunk.h"
#include "GLOBAL.h"
#include <vector>
void Chunk::clampKeys(std::vector<double> freq)
{
	Key k = GLOBAL::MUSICAL_KEY;
	std::vector<Keys> ref;
	switch (k)
	{
		case Key::A_MAJOR:
			ref = GLOBAL::aMajor;
			break;
		case Key::A_SHARP_MAJOR:
			ref = GLOBAL::aSharpMajor;
			break;
		case Key::B_MAJOR:
			ref = GLOBAL::bMajor;
			break;
		case Key::C_MAJOR:
			ref = GLOBAL::cMajor;
			break;
		case Key::C_SHARP_MAJOR:
			ref = GLOBAL::cSharpMajor;
			break;
		case Key::D_MAJOR:
			ref = GLOBAL::dMajor;
			break;
		case Key::D_SHARP_MAJOR:
			ref = GLOBAL::dSharpMajor;
			break;
		case Key::E_MAJOR:
			ref = GLOBAL::eMajor;
			break;
		case Key::F_MAJOR:
			ref = GLOBAL::fMajor;
			break;
		case Key::F_SHARP_MAJOR:
			ref = GLOBAL::fSharpMajor;
			break;
		case Key::G_MAJOR:
			ref = GLOBAL::gMajor;
			break;
		case Key::G_SHARP_MAJOR:
			ref = GLOBAL::gSharpMajor;
			break;
		case Key::NO_KEY:
			break;
	}
	


}