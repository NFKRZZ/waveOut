#include "MidiMaker.h"
#include <string>
#include <iostream>
using namespace std;

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
