#include "src/Application.h"

#include <windows.h>

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE,
                      _In_ LPWSTR, _In_ int showCommand) {
    xiaochuang::Application application;
    return application.Run(instance, showCommand);
}
