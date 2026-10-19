// Registro do módulo no Unreal. Fica numa subpasta para o CMake dos testes (que compila
// apenas Private/*.cpp) não precisar do motor.
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, VilaCore);
