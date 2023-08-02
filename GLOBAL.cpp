#include "Keys.h"
#include "GLOBAL.h"
#include <vector>

using namespace std;

int GLOBAL::sampleRate = 0;
float GLOBAL::qBeatDuration = 0;
float GLOBAL::twoBeatDuration = 0;
Key GLOBAL::MUSICAL_KEY = Key::NO_KEY;

vector<Keys> GLOBAL::cMajor = {
    Keys::C_0,
    Keys::D_0,
    Keys::E_0,
    Keys::F_0,
    Keys::G_0,
    Keys::A_0,
    Keys::B_0,
    Keys::C_1,
    Keys::D_1,
    Keys::E_1,
    Keys::F_1,
    Keys::G_1,
    Keys::A_1,
    Keys::B_1,
    Keys::C_2,
    Keys::D_2,
    Keys::E_2,
    Keys::F_2,
    Keys::G_2,
    Keys::A_2,
    Keys::B_2,
    Keys::C_3,
    Keys::D_3,
    Keys::E_3,
    Keys::F_3,
    Keys::G_3,
    Keys::A_3,
    Keys::B_3,
    Keys::C_4,
    Keys::D_4,
    Keys::E_4,
    Keys::F_4,
    Keys::G_4,
    Keys::A_4,
    Keys::B_4,
    Keys::C_5,
    Keys::D_5,
    Keys::E_5,
    Keys::F_5,
    Keys::G_5,
    Keys::A_5,
    Keys::B_5,
    Keys::C_6,
    Keys::D_6,
    Keys::E_6,
    Keys::F_6,
    Keys::G_6,
    Keys::A_6,
    Keys::B_6,
    Keys::C_7,
    Keys::D_7,
    Keys::E_7,
    Keys::F_7,
    Keys::G_7,
    Keys::A_7,
    Keys::B_7,
    Keys::C_8,
    Keys::D_8,
    Keys::E_8,
    Keys::F_8,
    Keys::G_8,
    Keys::A_8,
    Keys::B_8,
    Keys::C_9
};
vector<Keys> GLOBAL::cSharpMajor = {
    Keys::C_SHARP_0,
    Keys::D_SHARP_0,
    Keys::F_0,
    Keys::F_SHARP_0,
    Keys::G_SHARP_0,
    Keys::A_SHARP_0,
    Keys::C_1,
    Keys::D_1,
    Keys::E_1,
    Keys::F_1,
    Keys::G_1,
    Keys::A_1,
    Keys::B_1,
    Keys::C_SHARP_1,
    Keys::D_SHARP_1,
    Keys::F_1,
    Keys::F_SHARP_1,
    Keys::G_SHARP_1,
    Keys::A_SHARP_1,
    Keys::C_2,
    Keys::D_2,
    Keys::E_2,
    Keys::F_2,
    Keys::G_2,
    Keys::A_2,
    Keys::B_2,
    Keys::C_SHARP_2,
    Keys::D_SHARP_2,
    Keys::F_2,
    Keys::F_SHARP_2,
    Keys::G_SHARP_2,
    Keys::A_SHARP_2,
    Keys::C_3,
    Keys::D_3,
    Keys::E_3,
    Keys::F_3,
    Keys::G_3,
    Keys::A_3,
    Keys::B_3,
    Keys::C_SHARP_3,
    Keys::D_SHARP_3,
    Keys::F_3,
    Keys::F_SHARP_3,
    Keys::G_SHARP_3,
    Keys::A_SHARP_3,
    Keys::C_4,
    Keys::D_4,
    Keys::E_4,
    Keys::F_4,
    Keys::G_4,
    Keys::A_4,
    Keys::B_4,
    Keys::C_SHARP_4,
    Keys::D_SHARP_4,
    Keys::F_4,
    Keys::F_SHARP_4,
    Keys::G_SHARP_4,
    Keys::A_SHARP_4,
    Keys::C_5,
    Keys::D_5,
    Keys::E_5,
    Keys::F_5,
    Keys::G_5,
    Keys::A_5,
    Keys::B_5,
    Keys::C_SHARP_5,
    Keys::D_SHARP_5,
    Keys::F_5,
    Keys::F_SHARP_5,
    Keys::G_SHARP_5,
    Keys::A_SHARP_5,
    Keys::C_6,
    Keys::D_6,
    Keys::E_6,
    Keys::F_6,
    Keys::G_6,
    Keys::A_6,
    Keys::B_6,
    Keys::C_SHARP_6,
    Keys::D_SHARP_6,
    Keys::F_6,
    Keys::F_SHARP_6,
    Keys::G_SHARP_6,
    Keys::A_SHARP_6,
    Keys::C_7,
    Keys::D_7,
    Keys::E_7,
    Keys::F_7,
    Keys::G_7,
    Keys::A_7,
    Keys::B_7,
    Keys::C_SHARP_7,
    Keys::D_SHARP_7,
    Keys::F_7,
    Keys::F_SHARP_7,
    Keys::G_SHARP_7,
    Keys::A_SHARP_7,
    Keys::C_8,
    Keys::D_8,
    Keys::E_8,
    Keys::F_8,
    Keys::G_8,
    Keys::A_8,
    Keys::B_8,
    Keys::C_SHARP_8,
    Keys::D_SHARP_8,
    Keys::F_8,
    Keys::F_SHARP_8,
    Keys::G_SHARP_8,
    Keys::A_SHARP_8,
    Keys::C_9,
    Keys::D_9,
    Keys::E_9,
    Keys::F_9,
    Keys::G_9,
    Keys::A_9,
    Keys::B_9,
    Keys::C_SHARP_9,
    Keys::D_SHARP_9,
    Keys::F_9,
    Keys::F_SHARP_9,
    Keys::G_SHARP_9,
    Keys::A_SHARP_9,
    Keys::C_9
};
vector<Keys> GLOBAL::dMajor = {
    Keys::D_0,
    Keys::E_0,
    Keys::F_SHARP_0,
    Keys::G_0,
    Keys::A_0,
    Keys::B_0,
    Keys::C_SHARP_1,
    Keys::D_1,
    Keys::E_1,
    Keys::F_SHARP_1,
    Keys::G_1,
    Keys::A_1,
    Keys::B_1,
    Keys::C_SHARP_2,
    Keys::D_2,
    Keys::E_2,
    Keys::F_SHARP_2,
    Keys::G_2,
    Keys::A_2,
    Keys::B_2,
    Keys::C_SHARP_3,
    Keys::D_3,
    Keys::E_3,
    Keys::F_SHARP_3,
    Keys::G_3,
    Keys::A_3,
    Keys::B_3,
    Keys::C_SHARP_4,
    Keys::D_4,
    Keys::E_4,
    Keys::F_SHARP_4,
    Keys::G_4,
    Keys::A_4,
    Keys::B_4,
    Keys::C_SHARP_5,
    Keys::D_5,
    Keys::E_5,
    Keys::F_SHARP_5,
    Keys::G_5,
    Keys::A_5,
    Keys::B_5,
    Keys::C_SHARP_6,
    Keys::D_6,
    Keys::E_6,
    Keys::F_SHARP_6,
    Keys::G_6,
    Keys::A_6,
    Keys::B_6,
    Keys::C_SHARP_7,
    Keys::D_7,
    Keys::E_7,
    Keys::F_SHARP_7,
    Keys::G_7,
    Keys::A_7,
    Keys::B_7,
    Keys::C_SHARP_8,
    Keys::D_8,
    Keys::E_8,
    Keys::F_SHARP_8,
    Keys::G_8,
    Keys::A_8,
    Keys::B_8,
    Keys::C_SHARP_9,
    Keys::D_9,
    Keys::E_9,
    Keys::F_SHARP_9,
    Keys::G_9,
    Keys::A_9,
    Keys::B_9
};
vector<Keys> GLOBAL::dSharpMajor = {
    Keys::D_SHARP_0,
    Keys::F_0,
    Keys::G_0,
    Keys::G_SHARP_0,
    Keys::A_SHARP_0,
    Keys::C_1,
    Keys::D_1,
    Keys::D_SHARP_1,
    Keys::F_1,
    Keys::G_1,
    Keys::G_SHARP_1,
    Keys::A_SHARP_1,
    Keys::C_2,
    Keys::D_2,
    Keys::D_SHARP_2,
    Keys::F_2,
    Keys::G_2,
    Keys::G_SHARP_2,
    Keys::A_SHARP_2,
    Keys::C_3,
    Keys::D_3,
    Keys::D_SHARP_3,
    Keys::F_3,
    Keys::G_3,
    Keys::G_SHARP_3,
    Keys::A_SHARP_3,
    Keys::C_4,
    Keys::D_4,
    Keys::D_SHARP_4,
    Keys::F_4,
    Keys::G_4,
    Keys::G_SHARP_4,
    Keys::A_SHARP_4,
    Keys::C_5,
    Keys::D_5,
    Keys::D_SHARP_5,
    Keys::F_5,
    Keys::G_5,
    Keys::G_SHARP_5,
    Keys::A_SHARP_5,
    Keys::C_6,
    Keys::D_6,
    Keys::D_SHARP_6,
    Keys::F_6,
    Keys::G_6,
    Keys::G_SHARP_6,
    Keys::A_SHARP_6,
    Keys::C_7,
    Keys::D_7,
    Keys::D_SHARP_7,
    Keys::F_7,
    Keys::G_7,
    Keys::G_SHARP_7,
    Keys::A_SHARP_7,
    Keys::C_8,
    Keys::D_8,
    Keys::D_SHARP_8,
    Keys::F_8,
    Keys::G_8,
    Keys::G_SHARP_8,
    Keys::A_SHARP_8,
    Keys::C_9,
    Keys::D_9,
    Keys::D_SHARP_9,
    Keys::F_9,
    Keys::G_9,
    Keys::G_SHARP_9,
    Keys::A_SHARP_9,
    Keys::C_9
};
vector<Keys> GLOBAL::eMajor = {
    Keys::E_0,
    Keys::F_SHARP_0,
    Keys::G_SHARP_0,
    Keys::A_0,
    Keys::B_0,
    Keys::C_SHARP_1,
    Keys::D_SHARP_1,
    Keys::E_1,
    Keys::F_SHARP_1,
    Keys::G_SHARP_1,
    Keys::A_1,
    Keys::B_1,
    Keys::C_SHARP_2,
    Keys::D_SHARP_2,
    Keys::E_2,
    Keys::F_SHARP_2,
    Keys::G_SHARP_2,
    Keys::A_2,
    Keys::B_2,
    Keys::C_SHARP_3,
    Keys::D_SHARP_3,
    Keys::E_3,
    Keys::F_SHARP_3,
    Keys::G_SHARP_3,
    Keys::A_3,
    Keys::B_3,
    Keys::C_SHARP_4,
    Keys::D_SHARP_4,
    Keys::E_4,
    Keys::F_SHARP_4,
    Keys::G_SHARP_4,
    Keys::A_4,
    Keys::B_4,
    Keys::C_SHARP_5,
    Keys::D_SHARP_5,
    Keys::E_5,
    Keys::F_SHARP_5,
    Keys::G_SHARP_5,
    Keys::A_5,
    Keys::B_5,
    Keys::C_SHARP_6,
    Keys::D_SHARP_6,
    Keys::E_6,
    Keys::F_SHARP_6,
    Keys::G_SHARP_6,
    Keys::A_6,
    Keys::B_6,
    Keys::C_SHARP_7,
    Keys::D_SHARP_7,
    Keys::E_7,
    Keys::F_SHARP_7,
    Keys::G_SHARP_7,
    Keys::A_7,
    Keys::B_7,
    Keys::C_SHARP_8,
    Keys::D_SHARP_8,
    Keys::E_8,
    Keys::F_SHARP_8,
    Keys::G_SHARP_8,
    Keys::A_8,
    Keys::B_8,
    Keys::C_SHARP_9,
    Keys::D_SHARP_9,
    Keys::E_9,
    Keys::F_SHARP_9,
    Keys::G_SHARP_9,
    Keys::A_9,
    Keys::B_9,
    Keys::C_SHARP_9
};
vector<Keys> GLOBAL::fMajor = {
    Keys::F_0,
    Keys::G_0,
    Keys::A_0,
    Keys::A_SHARP_0,
    Keys::C_1,
    Keys::D_1,
    Keys::E_1,
    Keys::F_1,
    Keys::G_1,
    Keys::A_1,
    Keys::A_SHARP_1,
    Keys::C_2,
    Keys::D_2,
    Keys::E_2,
    Keys::F_2,
    Keys::G_2,
    Keys::A_2,
    Keys::A_SHARP_2,
    Keys::C_3,
    Keys::D_3,
    Keys::E_3,
    Keys::F_3,
    Keys::G_3,
    Keys::A_3,
    Keys::A_SHARP_3,
    Keys::C_4,
    Keys::D_4,
    Keys::E_4,
    Keys::F_4,
    Keys::G_4,
    Keys::A_4,
    Keys::A_SHARP_4,
    Keys::C_5,
    Keys::D_5,
    Keys::E_5,
    Keys::F_5,
    Keys::G_5,
    Keys::A_5,
    Keys::A_SHARP_5,
    Keys::C_6,
    Keys::D_6,
    Keys::E_6,
    Keys::F_6,
    Keys::G_6,
    Keys::A_6,
    Keys::A_SHARP_6,
    Keys::C_7,
    Keys::D_7,
    Keys::E_7,
    Keys::F_7,
    Keys::G_7,
    Keys::A_7,
    Keys::A_SHARP_7,
    Keys::C_8,
    Keys::D_8,
    Keys::E_8,
    Keys::F_8,
    Keys::G_8,
    Keys::A_8,
    Keys::A_SHARP_8,
    Keys::C_9,
    Keys::D_9,
    Keys::E_9,
    Keys::F_9,
    Keys::G_9,
    Keys::A_9,
    Keys::A_SHARP_9,
    Keys::C_9
};
vector<Keys> GLOBAL::fSharpMajor = {
    Keys::F_SHARP_0,
    Keys::G_SHARP_0,
    Keys::A_SHARP_0,
    Keys::B_0,
    Keys::C_SHARP_1,
    Keys::D_SHARP_1,
    Keys::F_1,
    Keys::F_SHARP_1,
    Keys::G_SHARP_1,
    Keys::A_SHARP_1,
    Keys::B_1,
    Keys::C_SHARP_2,
    Keys::D_SHARP_2,
    Keys::F_2,
    Keys::F_SHARP_2,
    Keys::G_SHARP_2,
    Keys::A_SHARP_2,
    Keys::B_2,
    Keys::C_SHARP_3,
    Keys::D_SHARP_3,
    Keys::F_3,
    Keys::F_SHARP_3,
    Keys::G_SHARP_3,
    Keys::A_SHARP_3,
    Keys::B_3,
    Keys::C_SHARP_4,
    Keys::D_SHARP_4,
    Keys::F_4,
    Keys::F_SHARP_4,
    Keys::G_SHARP_4,
    Keys::A_SHARP_4,
    Keys::B_4,
    Keys::C_SHARP_5,
    Keys::D_SHARP_5,
    Keys::F_5,
    Keys::F_SHARP_5,
    Keys::G_SHARP_5,
    Keys::A_SHARP_5,
    Keys::B_5,
    Keys::C_SHARP_6,
    Keys::D_SHARP_6,
    Keys::F_6,
    Keys::F_SHARP_6,
    Keys::G_SHARP_6,
    Keys::A_SHARP_6,
    Keys::B_6,
    Keys::C_SHARP_7,
    Keys::D_SHARP_7,
    Keys::F_7,
    Keys::F_SHARP_7,
    Keys::G_SHARP_7,
    Keys::A_SHARP_7,
    Keys::B_7,
    Keys::C_SHARP_8,
    Keys::D_SHARP_8,
    Keys::F_8,
    Keys::F_SHARP_8,
    Keys::G_SHARP_8,
    Keys::A_SHARP_8,
    Keys::B_8,
    Keys::C_SHARP_9,
    Keys::D_SHARP_9,
    Keys::F_9,
    Keys::F_SHARP_9,
    Keys::G_SHARP_9,
    Keys::A_SHARP_9,
    Keys::B_9,
    Keys::C_SHARP_9
};
vector<Keys> GLOBAL::gMajor = {
    Keys::G_0,
    Keys::A_0,
    Keys::B_0,
    Keys::C_1,
    Keys::D_1,
    Keys::E_1,
    Keys::F_SHARP_1,
    Keys::G_1,
    Keys::A_1,
    Keys::B_1,
    Keys::C_2,
    Keys::D_2,
    Keys::E_2,
    Keys::F_SHARP_2,
    Keys::G_2,
    Keys::A_2,
    Keys::B_2,
    Keys::C_3,
    Keys::D_3,
    Keys::E_3,
    Keys::F_SHARP_3,
    Keys::G_3,
    Keys::A_3,
    Keys::B_3,
    Keys::C_4,
    Keys::D_4,
    Keys::E_4,
    Keys::F_SHARP_4,
    Keys::G_4,
    Keys::A_4,
    Keys::B_4,
    Keys::C_5,
    Keys::D_5,
    Keys::E_5,
    Keys::F_SHARP_5,
    Keys::G_5,
    Keys::A_5,
    Keys::B_5,
    Keys::C_6,
    Keys::D_6,
    Keys::E_6,
    Keys::F_SHARP_6,
    Keys::G_6,
    Keys::A_6,
    Keys::B_6,
    Keys::C_7,
    Keys::D_7,
    Keys::E_7,
    Keys::F_SHARP_7,
    Keys::G_7,
    Keys::A_7,
    Keys::B_7,
    Keys::C_8,
    Keys::D_8,
    Keys::E_8,
    Keys::F_SHARP_8,
    Keys::G_8,
    Keys::A_8,
    Keys::B_8,
    Keys::C_9,
    Keys::D_9,
    Keys::E_9,
    Keys::F_SHARP_9,
    Keys::G_9,
    Keys::A_9,
    Keys::B_9,
    Keys::C_9
};
vector<Keys> GLOBAL::gSharpMajor = {
    Keys::G_SHARP_0,
    Keys::A_SHARP_0,
    Keys::C_1,
    Keys::C_SHARP_1,
    Keys::D_SHARP_1,
    Keys::F_1,
    Keys::G_1,
    Keys::G_SHARP_1,
    Keys::A_SHARP_1,
    Keys::C_2,
    Keys::C_SHARP_2,
    Keys::D_SHARP_2,
    Keys::F_2,
    Keys::G_2,
    Keys::G_SHARP_2,
    Keys::A_SHARP_2,
    Keys::C_3,
    Keys::C_SHARP_3,
    Keys::D_SHARP_3,
    Keys::F_3,
    Keys::G_3,
    Keys::G_SHARP_3,
    Keys::A_SHARP_3,
    Keys::C_4,
    Keys::C_SHARP_4,
    Keys::D_SHARP_4,
    Keys::F_4,
    Keys::G_4,
    Keys::G_SHARP_4,
    Keys::A_SHARP_4,
    Keys::C_5,
    Keys::C_SHARP_5,
    Keys::D_SHARP_5,
    Keys::F_5,
    Keys::G_5,
    Keys::G_SHARP_5,
    Keys::A_SHARP_5,
    Keys::C_6,
    Keys::C_SHARP_6,
    Keys::D_SHARP_6,
    Keys::F_6,
    Keys::G_6,
    Keys::G_SHARP_6,
    Keys::A_SHARP_6,
    Keys::C_7,
    Keys::C_SHARP_7,
    Keys::D_SHARP_7,
    Keys::F_7,
    Keys::G_7,
    Keys::G_SHARP_7,
    Keys::A_SHARP_7,
    Keys::C_8,
    Keys::C_SHARP_8,
    Keys::D_SHARP_8,
    Keys::F_8,
    Keys::G_8,
    Keys::G_SHARP_8,
    Keys::A_SHARP_8,
    Keys::C_9,
    Keys::C_SHARP_9,
    Keys::D_SHARP_9,
    Keys::F_9,
    Keys::G_9,
    Keys::G_SHARP_9,
    Keys::A_SHARP_9,
    Keys::C_9
};
vector<Keys> GLOBAL::aMajor = {
    Keys::A_0,
    Keys::B_0,
    Keys::C_SHARP_1,
    Keys::D_1,
    Keys::E_1,
    Keys::F_SHARP_1,
    Keys::G_1,
    Keys::A_1,
    Keys::B_1,
    Keys::C_SHARP_2,
    Keys::D_2,
    Keys::E_2,
    Keys::F_SHARP_2,
    Keys::G_2,
    Keys::A_2,
    Keys::B_2,
    Keys::C_SHARP_3,
    Keys::D_3,
    Keys::E_3,
    Keys::F_SHARP_3,
    Keys::G_3,
    Keys::A_3,
    Keys::B_3,
    Keys::C_SHARP_4,
    Keys::D_4,
    Keys::E_4,
    Keys::F_SHARP_4,
    Keys::G_4,
    Keys::A_4,
    Keys::B_4,
    Keys::C_SHARP_5,
    Keys::D_5,
    Keys::E_5,
    Keys::F_SHARP_5,
    Keys::G_5,
    Keys::A_5,
    Keys::B_5,
    Keys::C_SHARP_6,
    Keys::D_6,
    Keys::E_6,
    Keys::F_SHARP_6,
    Keys::G_6,
    Keys::A_6,
    Keys::B_6,
    Keys::C_SHARP_7,
    Keys::D_7,
    Keys::E_7,
    Keys::F_SHARP_7,
    Keys::G_7,
    Keys::A_7,
    Keys::B_7,
    Keys::C_SHARP_8,
    Keys::D_8,
    Keys::E_8,
    Keys::F_SHARP_8,
    Keys::G_8,
    Keys::A_8,
    Keys::B_8,
    Keys::C_SHARP_9,
    Keys::D_9,
    Keys::E_9,
    Keys::F_SHARP_9,
    Keys::G_9,
    Keys::A_9,
    Keys::B_9,
    Keys::C_SHARP_9
};
vector<Keys> GLOBAL::aSharpMajor = {
    Keys::A_SHARP_0,
    Keys::C_1,
    Keys::D_1,
    Keys::D_SHARP_1,
    Keys::F_1,
    Keys::G_1,
    Keys::A_1,
    Keys::A_SHARP_1,
    Keys::C_2,
    Keys::D_2,
    Keys::D_SHARP_2,
    Keys::F_2,
    Keys::G_2,
    Keys::A_2,
    Keys::A_SHARP_2,
    Keys::C_3,
    Keys::D_3,
    Keys::D_SHARP_3,
    Keys::F_3,
    Keys::G_3,
    Keys::A_3,
    Keys::A_SHARP_3,
    Keys::C_4,
    Keys::D_4,
    Keys::D_SHARP_4,
    Keys::F_4,
    Keys::G_4,
    Keys::A_4,
    Keys::A_SHARP_4,
    Keys::C_5,
    Keys::D_5,
    Keys::D_SHARP_5,
    Keys::F_5,
    Keys::G_5,
    Keys::A_5,
    Keys::A_SHARP_5,
    Keys::C_6,
    Keys::D_6,
    Keys::D_SHARP_6,
    Keys::F_6,
    Keys::G_6,
    Keys::A_6,
    Keys::A_SHARP_6,
    Keys::C_7,
    Keys::D_7,
    Keys::D_SHARP_7,
    Keys::F_7,
    Keys::G_7,
    Keys::A_7,
    Keys::A_SHARP_7,
    Keys::C_8,
    Keys::D_8,
    Keys::D_SHARP_8,
    Keys::F_8,
    Keys::G_8,
    Keys::A_8,
    Keys::A_SHARP_8,
    Keys::C_9,
    Keys::D_9,
    Keys::D_SHARP_9,
    Keys::F_9,
    Keys::G_9,
    Keys::A_9,
    Keys::A_SHARP_9,
    Keys::C_9
};
vector<Keys> GLOBAL::bMajor = {
    Keys::B_0,
    Keys::C_SHARP_1,
    Keys::D_SHARP_1,
    Keys::E_1,
    Keys::F_SHARP_1,
    Keys::G_SHARP_1,
    Keys::A_1,
    Keys::B_1,
    Keys::C_SHARP_2,
    Keys::D_SHARP_2,
    Keys::E_2,
    Keys::F_SHARP_2,
    Keys::G_SHARP_2,
    Keys::A_2,
    Keys::B_2,
    Keys::C_SHARP_3,
    Keys::D_SHARP_3,
    Keys::E_3,
    Keys::F_SHARP_3,
    Keys::G_SHARP_3,
    Keys::A_3,
    Keys::B_3,
    Keys::C_SHARP_4,
    Keys::D_SHARP_4,
    Keys::E_4,
    Keys::F_SHARP_4,
    Keys::G_SHARP_4,
    Keys::A_4,
    Keys::B_4,
    Keys::C_SHARP_5,
    Keys::D_SHARP_5,
    Keys::E_5,
    Keys::F_SHARP_5,
    Keys::G_SHARP_5,
    Keys::A_5,
    Keys::B_5,
    Keys::C_SHARP_6,
    Keys::D_SHARP_6,
    Keys::E_6,
    Keys::F_SHARP_6,
    Keys::G_SHARP_6,
    Keys::A_6,
    Keys::B_6,
    Keys::C_SHARP_7,
    Keys::D_SHARP_7,
    Keys::E_7,
    Keys::F_SHARP_7,
    Keys::G_SHARP_7,
    Keys::A_7,
    Keys::B_7,
    Keys::C_SHARP_8,
    Keys::D_SHARP_8,
    Keys::E_8,
    Keys::F_SHARP_8,
    Keys::G_SHARP_8,
    Keys::A_8,
    Keys::B_8,
    Keys::C_SHARP_9,
    Keys::D_SHARP_9,
    Keys::E_9,
    Keys::F_SHARP_9,
    Keys::G_SHARP_9,
    Keys::A_9,
    Keys::B_9,
    Keys::C_SHARP_9
};