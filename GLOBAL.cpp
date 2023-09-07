#include "Keys.h"
#include "GLOBAL.h"


using namespace std;

int GLOBAL::sampleRate = 0;
float GLOBAL::qBeatDuration = 0;
float GLOBAL::twoBeatDuration = 0;
Key GLOBAL::MUSICAL_KEY = Key::NO_KEY;
bool GLOBAL::isMonophonic = false;
int GLOBAL::chordVoices = 0;
float GLOBAL::SONG_LENGTH = 0;

bool GLOBAL::Init()
{
    if (isMonophonic && chordVoices > 1)
    {
        return false;
    }
    else
    {
        return true;
    }
}

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
    Keys::C_SHARP_0,
    Keys::D_0,
    Keys::E_0,
    Keys::F_SHARP_0,
    Keys::G_SHARP_0,
    Keys::A_0,
    Keys::B_0,
    Keys::C_SHARP_1,
    Keys::D_1,
    Keys::E_1,
    Keys::F_SHARP_1,
    Keys::G_SHARP_1,
    Keys::A_1,
    Keys::B_1,
    Keys::C_SHARP_2,
    Keys::D_2,
    Keys::E_2,
    Keys::F_SHARP_2,
    Keys::G_SHARP_2,
    Keys::A_2,
    Keys::B_2,
    Keys::C_SHARP_3,
    Keys::D_3,
    Keys::E_3,
    Keys::F_SHARP_3,
    Keys::G_SHARP_3,
    Keys::A_3,
    Keys::B_3,
    Keys::C_SHARP_4,
    Keys::D_4,
    Keys::E_4,
    Keys::F_SHARP_4,
    Keys::G_SHARP_4,
    Keys::A_4,
    Keys::B_4,
    Keys::C_SHARP_5,
    Keys::D_5,
    Keys::E_5,
    Keys::F_SHARP_5,
    Keys::G_SHARP_5,
    Keys::A_5,
    Keys::B_5,
    Keys::C_SHARP_6,
    Keys::D_6,
    Keys::E_6,
    Keys::F_SHARP_6,
    Keys::G_SHARP_6,
    Keys::A_6,
    Keys::B_6,
    Keys::C_SHARP_7,
    Keys::D_7,
    Keys::E_7,
    Keys::F_SHARP_7,
    Keys::G_SHARP_7,
    Keys::A_7,
    Keys::B_7,
    Keys::C_SHARP_8,
    Keys::D_8,
    Keys::E_8,
    Keys::F_SHARP_8,
    Keys::G_SHARP_8,
    Keys::A_8,
    Keys::B_8,
    Keys::C_SHARP_9,
    Keys::D_9,
    Keys::E_9,
    Keys::F_SHARP_9,
    Keys::G_SHARP_9,
    Keys::A_9,
    Keys::B_9
};               //FIXED
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
vector<pair<string, int>> GLOBAL::keysString = {
    {"C_0", 16}, {"C_SHARP_0", 17}, {"D_0", 18}, {"D_SHARP_0", 19}, {"E_0", 21},
       {"F_0", 22}, {"F_SHARP_0", 23}, {"G_0", 24}, {"G_SHARP_0", 26}, {"A_0", 28},
       {"A_SHARP_0", 29}, {"B_0", 31}, {"C_1", 33}, {"C_SHARP_1", 35}, {"D_1", 37},
       {"D_SHARP_1", 39}, {"E_1", 41}, {"F_1", 44}, {"F_SHARP_1", 46}, {"G_1", 49},
       {"G_SHARP_1", 52}, {"A_1", 55}, {"A_SHARP_1", 58}, {"B_1", 62}, {"C_2", 65},
       {"C_SHARP_2", 69}, {"D_2", 73}, {"D_SHARP_2", 78}, {"E_2", 82}, {"F_2", 87},
       {"F_SHARP_2", 93}, {"G_2", 98}, {"G_SHARP_2", 104}, {"A_2", 110}, {"A_SHARP_2", 117},
       {"B_2", 123}, {"C_3", 131}, {"C_SHARP_3", 139}, {"D_3", 147}, {"D_SHARP_3", 156},
       {"E_3", 165}, {"F_3", 175}, {"F_SHARP_3", 185}, {"G_3", 196}, {"G_SHARP_3", 208},
       {"A_3", 220}, {"A_SHARP_3", 233}, {"B_3", 247}, {"C_4", 262}, {"C_SHARP_4", 277},
       {"D_4", 294}, {"D_SHARP_4", 311}, {"E_4", 330}, {"F_4", 349}, {"F_SHARP_4", 370},
       {"G_4", 392}, {"G_SHARP_4", 415}, {"A_4", 440}, {"A_SHARP_4", 466}, {"B_4", 494},
       {"C_5", 523}, {"C_SHARP_5", 554}, {"D_5", 587}, {"D_SHARP_5", 622}, {"E_5", 659},
       {"F_5", 698}, {"F_SHARP_5", 740}, {"G_5", 784}, {"G_SHARP_5", 831}, {"A_5", 880},
       {"A_SHARP_5", 932}, {"B_5", 988}, {"C_6", 1047}, {"C_SHARP_6", 1109}, {"D_6", 1175},
       {"D_SHARP_6", 1245}, {"E_6", 1319}, {"F_6", 1397}, {"F_SHARP_6", 1480}, {"G_6", 1568},
       {"G_SHARP_6", 1661}, {"A_6", 1760}, {"A_SHARP_6", 1865}, {"B_6", 1976}, {"C_7", 2093},
       {"C_SHARP_7", 2217}, {"D_7", 2349}, {"D_SHARP_7", 2489}, {"E_7", 2637}, {"F_7", 2794},
       {"F_SHARP_7", 2960}, {"G_7", 3136}, {"G_SHARP_7", 3322}, {"A_7", 3520}, {"A_SHARP_7", 3729},
       {"B_7", 3951}, {"C_8", 4186}, {"C_SHARP_8", 4435}, {"D_8", 4699}, {"D_SHARP_8", 4978},
       {"E_8", 5274}, {"F_8", 5588}, {"F_SHARP_8", 5920}, {"G_8", 6272}, {"G_SHARP_8", 6645},
       {"A_8", 7040}, {"A_SHARP_8", 7459}, {"B_8", 7902}, {"C_9", 8372}, {"C_SHARP_9", 8870},
       {"D_9", 9397}, {"D_SHARP_9", 9956}, {"E_9", 10548}, {"F_9", 11175}, {"F_SHARP_9", 11840},
       {"G_9", 12544}, {"G_SHARP_9", 13290}, {"A_9", 14080}, {"A_SHARP_9", 14917}, {"B_9", 15804}
};
vector<pair<string, int>> GLOBAL::keyString = {
    {"NO_KEY",0},{"C_MAJOR",1},{"C_SHARP_MAJOR",2},{"D_MAJOR",3},{"D_SHARP_MAJOR",4},
    {"E_MAJOR",5},{"F_MAJOR",6},{"F_SHARP_MAJOR",7},{"G_MAJOR",8},{"G_SHARP_MAJOR",9},
    {"A_MAJOR",10},{"A_SHARP_MAJOR",11},{"B_MAJOR",12}
};


