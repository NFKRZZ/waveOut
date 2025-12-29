#include "StemSeperator.h"
#include <sstream>     // for std::ostringstream
#include <iostream>    // for std::cout, std::cerr
#include <cstdio>      // for FILE, popen, pclose, fgets
void StemSeperator::split(const std::string& path)
{
	std::ostringstream cmd;

    #ifdef _WIN32
        cmd << "python demucs_script.py \"" << path << "\" 2>&1";
    #else
        cmd << "python3 demucs_script.py '" << path << "' 2>&1";
    #endif

    FILE* pipe = _popen(cmd.str().c_str(), "r");

    if (!pipe)
    {
        std::cerr << "Failed to run DEMUCS python script" << std::endl;
        return;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        std::cout << buffer;
    }

    int exitCode = _pclose(pipe);
    std::cout << "Demucs finished with code: " << exitCode << std::endl;

}
