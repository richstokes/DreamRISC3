#pragma once

#include <fstream>
#include <optional>
#include <vector>

std::vector<char> scramble(std::vector<char>& unscrambled_bin);

std::vector<char> unscramble(std::vector<char>& scrambled_bin);
