#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <memory>
#include <cctype>

template<typename CharT>
struct SemVerT {
	typedef std::basic_string<CharT> string_t;
	int major = 0;
	int minor = 0;
	int patch = 0;
	std::vector<string_t> preRelease;
	string_t buildMetadata;

	// compare compares two SemVerT objects according to the SemVerT specification.
	// -1: this < other
	// 0: this == other
	// 1: this > other
	int compare(const SemVerT& other) const {
		if (major != other.major) return major > other.major ? 1 : -1;
		if (minor != other.minor) return minor > other.minor ? 1 : -1;
		if (patch != other.patch) return patch > other.patch ? 1 : -1;

		// Pre-release comparison rules:
		// 1. Versions with pre-release have lower precedence than those without
		if (preRelease.empty() && !other.preRelease.empty()) return 1;
		if (!preRelease.empty() && other.preRelease.empty()) return -1;

		// 2. Compare identifiers one by one
		size_t len = (std::min)(preRelease.size(), other.preRelease.size());
		for (size_t i = 0; i < len; ++i) {
			bool isNum1 = isNumericIdentifier(preRelease[i]);
			bool isNum2 = isNumericIdentifier(other.preRelease[i]);

			if (isNum1 && isNum2) {
				int n1 = std::stoi(preRelease[i]);
				int n2 = std::stoi(other.preRelease[i]);
				if (n1 != n2) return n1 > n2 ? 1 : -1;
			} else if (isNum1 || isNum2) {
				// Numeric identifiers have lower precedence than alphanumeric identifiers
				return isNum1 ? -1 : 1;
			} else {
				// Both are alphanumeric: lexical comparison
				if (preRelease[i] != other.preRelease[i])
					return preRelease[i] > other.preRelease[i] ? 1 : -1;
			}
		}
		if (preRelease.size() != other.preRelease.size())
			return preRelease.size() > other.preRelease.size() ? 1 : -1;

		return 0;
	}

	bool operator<(const SemVerT& other) const { return compare(other) == -1; }
	bool operator==(const SemVerT& other) const { return compare(other) == 0; }
	bool operator>(const SemVerT& other) const { return compare(other) == 1; }
	bool operator<=(const SemVerT& other) const { return compare(other) <= 0; }
	bool operator>=(const SemVerT& other) const { return compare(other) >= 0; }
	bool operator!=(const SemVerT& other) const { return compare(other) != 0; }

private:
	// isNumericIdentifier checks if a string is a valid numeric identifier
	static bool isNumericIdentifier(const string_t& s) {
		if (s.empty()) return false;
		for (auto c : s) {
			if(c < static_cast<CharT>('0') || c > static_cast<CharT>('9')) return false;
		}
		return true;
	}
};

using SemVer = SemVerT<char>;
using WSemVer = SemVerT<wchar_t>;


// Auxiliary function: split a string by a delimiter.
template<typename CharT>
std::vector<std::basic_string<CharT>> split(const std::basic_string<CharT>& s, CharT delimiter) {
	std::vector<std::basic_string<CharT>> tokens;
	std::basic_string<CharT> token;
	std::basic_istringstream<CharT> tokenStream(s);
	while (std::getline(tokenStream, token, delimiter)) {
		tokens.push_back(token);
	}
	return tokens;
}

// Validation helper functions

// hasLeadingZero checks if a numeric string has leading zeros (e.g., "01", "001")
template<typename CharT>
bool hasLeadingZero(const std::basic_string<CharT>& s) {
	return s.length() > 1 && s[0] == static_cast<CharT>('0');
}

// isValidIdentifier checks if a string is a valid identifier
// Valid: [0-9A-Za-z-] and non-empty
template<typename CharT>
bool isValidIdentifier(const std::basic_string<CharT>& s) {
	if (s.empty()) return false;
	for (CharT c : s) {
		bool isDigit = (c >= static_cast<CharT>('0') && c <= static_cast<CharT>('9'));
		bool isUpper = (c >= static_cast<CharT>('A') && c <= static_cast<CharT>('Z'));
		bool isLower = (c >= static_cast<CharT>('a') && c <= static_cast<CharT>('z'));
		bool isHyphen = (c == static_cast<CharT>('-'));

		if (!isDigit && !isUpper && !isLower && !isHyphen) {
			return false;
		}
	}
	return true;
}

// isValidNumericPart checks if a string is a valid numeric version part (no leading zeros)
template<typename CharT>
bool isValidNumericPart(const std::basic_string<CharT>& s) {
	if (s.empty()) return false;
	for (auto c : s) {
		if(c < static_cast<CharT>('0') || c > static_cast<CharT>('9')) return false;
	}
	return !hasLeadingZero(s);
}

// parseSemVer parses a version string into a SemVerT object
// according to the SemVerT specification https://semver.org/
// 
// Parameters:
//   version: The version string to parse
//   strict: If false, allows "X.Y" format and treats it as "X.Y.0" (default: true)
// 
// Returns:
//   unique_ptr to SemVerT object if parsing succeeds, nullptr otherwise
template<typename CharT>
std::unique_ptr<SemVerT<CharT>> parseSemVer(const std::basic_string<CharT>& version, bool strict = true) {
	using string_t = std::basic_string<CharT>;

	std::unique_ptr<SemVerT<CharT>> sv(new SemVerT<CharT>());
	auto workingVersion = version;

	// 1. Handle Build Metadata (+)
	size_t plusPos = workingVersion.find(static_cast<CharT>('+'));
	if (plusPos != string_t::npos) {
		sv->buildMetadata = workingVersion.substr(plusPos + 1);
		workingVersion = workingVersion.substr(0, plusPos);

		// Validate build metadata identifiers
		if (sv->buildMetadata.empty()) {
			// Empty build metadata after '+' is invalid
			return nullptr;
		}

		auto metadataParts = split(sv->buildMetadata, static_cast<CharT>('.'));
		for (const auto& part : metadataParts) {
			if (!isValidIdentifier(part)) {
				return nullptr;
			}
		}
	}

	// 2. Handle Pre-release (-)
	size_t hyphenPos = workingVersion.find(static_cast<CharT>('-'));
	if (hyphenPos != string_t::npos) {
		auto preReleaseStr = workingVersion.substr(hyphenPos + 1);
		workingVersion = workingVersion.substr(0, hyphenPos);

		if (!preReleaseStr.empty()) {
			sv->preRelease = split(preReleaseStr, static_cast<CharT>('.'));

			// Validate pre-release identifiers
			for (const auto& identifier : sv->preRelease) {
				// Must be non-empty and contain only [0-9A-Za-z-]
				if (!isValidIdentifier(identifier)) {
					return nullptr;
				}

				// Numeric identifiers must not have leading zeros
				bool isNumeric = true;
				for (auto c : identifier) {
					if (c < static_cast<CharT>('0') || c > static_cast<CharT>('9')) {
						isNumeric = false;
						break;
					}
				}
				if (isNumeric && hasLeadingZero(identifier)) {
					return nullptr;
				}
			}
		} else {
			// Empty pre-release after '-' is invalid
			return nullptr;
		}
	}

	// 3. Handle Major.Minor.Patch
	auto mainParts = split(workingVersion, static_cast<CharT>('.'));

	// Validate format
	if (mainParts.size() < 2) {
		// Must have at least Major.Minor
		return nullptr;
	}

	if (strict && mainParts.size() < 3) {
		// Strict mode requires Major.Minor.Patch
		return nullptr;
	}

	if (mainParts.size() > 3) {
		// Too many parts
		return nullptr;
	}

	// Validate and parse Major
	if (!isValidNumericPart(mainParts[0])) {
		return nullptr;
	}
	try {
		sv->major = std::stoi(mainParts[0]);
	} catch (const std::out_of_range&) {
		return nullptr;
	}

	// Validate and parse Minor
	if (!isValidNumericPart(mainParts[1])) {
		return nullptr;
	}
	try {
		sv->minor = std::stoi(mainParts[1]);
	} catch (const std::out_of_range&) {
		return nullptr;
	}

	// Validate and parse Patch
	if (mainParts.size() >= 3) {
		if (!isValidNumericPart(mainParts[2])) {
			return nullptr;
		}
		try {
			sv->patch = std::stoi(mainParts[2]);
		} catch (const std::out_of_range&) {
			return nullptr;
		}
	} else {
		// Non-strict mode: default patch to 0 for "X.Y" format
		sv->patch = 0;
	}

	return sv;
}

// Convenience overloads for string literals
inline std::unique_ptr<SemVer> parseSemVer(const char* version, bool strict = true) {
	return parseSemVer<char>(std::string(version), strict);
}

inline std::unique_ptr<WSemVer> parseSemVer(const wchar_t* version, bool strict = true) {
	return parseSemVer<wchar_t>(std::wstring(version), strict);
}