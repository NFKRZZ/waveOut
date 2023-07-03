#include "MidiMaker.h"
#include "Keys.h"
#include "Chunk.h"
#include <string>
#include <iostream>
#include <fftw3.h>
#include "Functions.h"
#include "GLOBAL.h"
#include <iomanip>
using namespace std;

vector<Keys> cMajor = {
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
vector<Keys> cSharpMajor = {
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
vector<Keys> dMajor = {
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
vector<Keys> dSharpMajor = {
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
vector<Keys> eMajor = {
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
vector<Keys> fMajor = {
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
vector<Keys> fSharpMajor = {
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
vector<Keys> gMajor = {
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
vector<Keys> gSharpMajor = {
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
vector<Keys> aMajor = {
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
vector<Keys> aSharpMajor = {
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
vector<Keys> bMajor = {
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


FILE* Init(string filename)
{
    FILE* file;
    errno_t error = fopen_s(&file, filename.c_str(), "w");

    if (!file)
    {
        std::cerr << "Failed to create file" << std::endl;
    }

    return file;
}

vector<Chunk> MidiMaker::lowPass(vector<short int> lowPassData)
{
    int sampleSize = GLOBAL::twoBeatDuration * GLOBAL::sampleRate;
    int numOfChunks = lowPassData.size() / (sampleSize * 2);

    vector<vector<double>> sampleChunks;
    sampleChunks.resize(numOfChunks);
    vector<Chunk> chunkData;
    for (int i = 0; i < numOfChunks;i++)
    {
        for (int j = 0;j < sampleSize;j++)
        {
            sampleChunks[i].push_back(lowPassData[(i * sampleSize) + j]);
        }
    }
    //Do FFT
    for (int i = 0; i < numOfChunks;i++)
    {
        int N = sampleChunks[i].size();
        fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);

        for (int j = 0;j < N;j++)
        {
            in[j][0] = sampleChunks[i][j];
            in[j][1] = 0;
        }
        fftw_plan plan = fftw_plan_dft_1d(N, in, in, FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(plan);

        double highestMagnitudes[3] = { 0.0 };
        unsigned int maxIndices[3] = { 0 };

        for (unsigned int l = 0; l < N; ++l) {
            double magnitude = sqrt(in[l][0] * in[l][0] + in[l][1] * in[l][1]);

            // Check if the magnitude is higher than any of the current top three
            for (int i = 0; i < 3; ++i) {
                if (magnitude > highestMagnitudes[i]) {
                    // Shift the current values down the array to make room for the new magnitude
                    for (int j = 2; j > i; --j) {
                        highestMagnitudes[j] = highestMagnitudes[j - 1];
                        maxIndices[j] = maxIndices[j - 1];
                    }

                    // Store the new magnitude and index
                    highestMagnitudes[i] = magnitude;
                    maxIndices[i] = l;

                    break;  // No need to check the remaining elements
                }
            }
        }

        int sampleRate = GLOBAL::sampleRate;

        


        /*for (int ja = 0; ja < 3; ++ja) 
        {
            double frequency = static_cast<double>(maxIndices[ja]) * sampleRate / N;
            cout << i<<" Frequency " << ja + 1 << ": " << frequency << " Hz, Magnitude: " << highestMagnitudes[ja] << endl;
        }*/
        vector<double> Frequencies;
        vector<double> mag;
        for (int a = 0;a < 3;a++)
        {
            double freq = (double)maxIndices[a] * sampleRate / N;
            if (freq > sampleRate / 2)
            {
                freq = abs(freq - sampleRate);
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }
            else
            {
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }

        }
        Chunk c = Chunk(Frequencies, mag, i);
        chunkData.push_back(c);
    }

    std::cout << "This is the size of lowpass chunk vector: " << chunkData.size()<<endl;
    return chunkData;

}

vector<Chunk> MidiMaker::bandPass(vector<short int> bandPassData)
{
    int sampleSize = GLOBAL::qBeatDuration * GLOBAL::sampleRate;
    int numOfChunks = bandPassData.size() / (sampleSize * 2);

    vector<vector<double>> sampleChunks;
    sampleChunks.resize(numOfChunks);
    vector<Chunk> chunkData;
    for (int i = 0; i < numOfChunks;i++)
    {
        for (int j = 0;j < sampleSize;j++)
        {
            sampleChunks[i].push_back(bandPassData[(i * sampleSize) + j]);
        }
    }
    //Do FFT
    for (int i = 0; i < numOfChunks;i++)
    {
        int N = sampleChunks[i].size();
        fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);

        for (int j = 0;j < N;j++)
        {
            in[j][0] = sampleChunks[i][j];
            in[j][1] = 0;
        }
        fftw_plan plan = fftw_plan_dft_1d(N, in, in, FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(plan);

        double highestMagnitudes[3] = { 0.0 };
        unsigned int maxIndices[3] = { 0 };

        for (unsigned int l = 0; l < N; ++l) {
            double magnitude = sqrt(in[l][0] * in[l][0] + in[l][1] * in[l][1]);

            // Check if the magnitude is higher than any of the current top three
            for (int i = 0; i < 3; ++i) {
                if (magnitude > highestMagnitudes[i]) {
                    // Shift the current values down the array to make room for the new magnitude
                    for (int j = 2; j > i; --j) {
                        highestMagnitudes[j] = highestMagnitudes[j - 1];
                        maxIndices[j] = maxIndices[j - 1];
                    }

                    // Store the new magnitude and index
                    highestMagnitudes[i] = magnitude;
                    maxIndices[i] = l;

                    break;  // No need to check the remaining elements
                }
            }
        }

        int sampleRate = GLOBAL::sampleRate;




        /*for (int ja = 0; ja < 3; ++ja)
        {
            double frequency = static_cast<double>(maxIndices[ja]) * sampleRate / N;
            cout << i<<" Frequency " << ja + 1 << ": " << frequency << " Hz, Magnitude: " << highestMagnitudes[ja] << endl;
        }*/
        vector<double> Frequencies;
        vector<double> mag;
        for (int a = 0;a < 3;a++)
        {
            double freq = (double)maxIndices[a] * sampleRate / N;
            if (freq > sampleRate / 2)
            {
                freq = abs(freq - sampleRate);
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }
            else
            {
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }

        }
        Chunk c = Chunk(Frequencies, mag, i);
        chunkData.push_back(c);
    }

    std::cout << "This is the size of bandpass chunk vector: " << chunkData.size() << endl;
    return chunkData;

}

vector<Chunk> MidiMaker::highPass(vector<short int> highPassData)
{
    int sampleSize = GLOBAL::qBeatDuration * GLOBAL::sampleRate;
    cout << "THIS IS SAMPLE SIZE HIGH PASS MIDI: " << sampleSize << endl;
    int numOfChunks = highPassData.size() / (sampleSize * 2);

    vector<vector<double>> sampleChunks;
    sampleChunks.resize(numOfChunks);
    vector<Chunk> chunkData;
    for (int i = 0; i < numOfChunks;i++)
    {
        for (int j = 0;j < sampleSize;j++)
        {
            sampleChunks[i].push_back(highPassData[(i * sampleSize) + j]);
        }
    }
    //Do FFT
    for (int i = 0; i < numOfChunks;i++)
    {
        int N = sampleChunks[i].size();
        fftw_complex* in = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * N);

        for (int j = 0;j < N;j++)
        {
            in[j][0] = sampleChunks[i][j];
            in[j][1] = 0;
        }
        fftw_plan plan = fftw_plan_dft_1d(N, in, in, FFTW_FORWARD, FFTW_ESTIMATE);
        fftw_execute(plan);

        double highestMagnitudes[3] = { 0.0 };
        unsigned int maxIndices[3] = { 0 };

        for (unsigned int l = 0; l < N; ++l) {
            double magnitude = sqrt(in[l][0] * in[l][0] + in[l][1] * in[l][1]);

            // Check if the magnitude is higher than any of the current top three
            for (int i = 0; i < 3; ++i) {
                if (magnitude > highestMagnitudes[i]) {
                    // Shift the current values down the array to make room for the new magnitude
                    for (int j = 2; j > i; --j) {
                        highestMagnitudes[j] = highestMagnitudes[j - 1];
                        maxIndices[j] = maxIndices[j - 1];
                    }

                    // Store the new magnitude and index
                    highestMagnitudes[i] = magnitude;
                    maxIndices[i] = l;

                    break;  // No need to check the remaining elements
                }
            }
        }

        int sampleRate = GLOBAL::sampleRate;




        /*for (int ja = 0; ja < 3; ++ja)
        {
            double frequency = static_cast<double>(maxIndices[ja]) * sampleRate / N;
            cout << i<<" Frequency " << ja + 1 << ": " << frequency << " Hz, Magnitude: " << highestMagnitudes[ja] << endl;
        }*/
        vector<double> Frequencies;
        vector<double> mag;
        for (int a = 0;a < 3;a++)
        {
            double freq = (double)maxIndices[a] * sampleRate / N;
            if (freq > sampleRate / 2)
            {
                freq = abs(freq - sampleRate);
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }
            else
            {
                Frequencies.push_back(freq);
                mag.push_back(highestMagnitudes[a]);
            }

        }
        Chunk c = Chunk(Frequencies, mag, i);
        chunkData.push_back(c);
    }

    std::cout << "This is the size of highpass chunk vector: " << chunkData.size() << endl;
    return chunkData;

}

