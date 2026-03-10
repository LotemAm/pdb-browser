#include "app/App.h"

#include <SDL3/SDL_main.h>

#include <cstdlib>

int main(int /*argc*/, char* /*argv*/[])
{
    App app;
    if (!app.init())
        return EXIT_FAILURE;
    app.run();
    return EXIT_SUCCESS;
}
