#pragma once
#include "wire.h"
namespace update_lab {
Bytes ReadInput(fs::path const& path);
void WriteOutput(fs::path const& path, Bytes const& bytes);
int PublishStore(int argc, char** argv);
}
