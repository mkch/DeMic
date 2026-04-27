#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include <limits>
#include <openssl/rand.h>

#undef max

template<class CharT>
static std::basic_string<CharT> GenerateRandomCode(const std::basic_string_view<CharT> candidateChars, size_t codeLength) {
	if(codeLength == 0) {
		throw std::invalid_argument("codeLength must be greater than 0");
	}
	if (candidateChars.size() > 256) {
		throw std::invalid_argument("candidateChars.size() must not exceed 256");
	}
	if (codeLength > static_cast<size_t>(std::numeric_limits<int>::max())) {
		throw std::invalid_argument("codeLength is too large");
	}
	std::vector<unsigned char> buf(codeLength);
	if (RAND_bytes(buf.data(), static_cast<int>(codeLength)) != 1) {
		throw std::runtime_error("RAND_bytes failed");
	}
	std::basic_string<CharT> code(codeLength, CharT{});
	for (size_t i = 0; i < codeLength; i++) {
		code[i] = candidateChars[buf[i] % candidateChars.size()];
	}
	return code;
}