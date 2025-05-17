#pragma once
#include <Arduino.h>
#include <vector>

std::vector<std::pair<String, String>> handleRequest(const String& path, const std::vector<std::pair<String, String>>& params);
std::vector<std::pair<String, String>> handleRequestHTML(const String& fullPath);
std::vector<std::pair<String, String>> handleRequestCLI(const String& input);
