#pragma once
#include <cstddef>
#include <span>
#include <string>
class ILTStream;
// Validate the complete linked ABC chunk directory and all records the
// runtime consumes before constructing native arrays or replacing a model.
bool model_ValidateFile(std::span<const std::byte>, std::string* reason = nullptr);
bool model_ValidateStream(ILTStream*, std::string* reason = nullptr);
