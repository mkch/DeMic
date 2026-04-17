#pragma once
#include <string>
#include <random>
#include <chrono>
#include <sstream>

template<class CharT>
static std::basic_string<CharT> GenerateRandomCode(const std::basic_string_view<CharT> candidateChars, size_t codeLength) {
	std::random_device rd;
	std::basic_stringstream<CharT> code;
	for (size_t i = 0; i < codeLength; i++) {
		code << candidateChars[rd() % candidateChars.size()];
	}
	return code.str();
}