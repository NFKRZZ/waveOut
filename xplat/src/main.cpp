#include "App.h"

#include <string>

int main(int argc, char** argv)
{
    std::string audioPath;
    if (argc > 1 && argv && argv[1])
        audioPath = argv[1];

    App app(audioPath);
    return app.Run();
}
