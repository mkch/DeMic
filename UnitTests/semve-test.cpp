#include "pch.h"
#include "../Semver.h"

// ============================================================================
// Helper Templates for Character Type Abstraction
// ============================================================================

// Convert char* literal to std::basic_string<CharT>
// Primary template declaration
template<typename CharT>
std::basic_string<CharT> S(const char* str);

// Specialization for char
template<>
std::basic_string<char> S<char>(const char* str) {
    return std::string(str);
}

// Specialization for wchar_t
template<>
std::basic_string<wchar_t> S<wchar_t>(const char* str) {
    std::wstring result;
    for (const char* p = str; *p; ++p) {
        result += static_cast<wchar_t>(*p);
    }
    return result;
}

// ============================================================================
// Typed Test Suite for Both char and wchar_t
// ============================================================================

template <typename T>
class SemVerTypedTest : public ::testing::Test {
public:
    using CharType = T;
    using StringType = std::basic_string<CharType>;

    // Helper: Parse version string
    auto parse(const char* version, bool strict = true) {
        return parseSemVer(S<CharType>(version), strict);
    }

    // Helper: Create string
    StringType str(const char* s) {
        return S<CharType>(s);
    }
};

// Register both char and wchar_t as types to test
using CharTypes = ::testing::Types<char, wchar_t>;
TYPED_TEST_CASE(SemVerTypedTest, CharTypes );  // Google Test 1.8 uses TYPED_TEST_CASE

// ============================================================================
// Parse Tests - Valid Versions
// ============================================================================

TYPED_TEST(SemVerTypedTest, ParseBasicVersion) {
    auto v = this->parse("1.2.3");
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(1, v->major);
    EXPECT_EQ(2, v->minor);
    EXPECT_EQ(3, v->patch);
    EXPECT_TRUE(v->preRelease.empty());
    EXPECT_TRUE(v->buildMetadata.empty());
}

TYPED_TEST(SemVerTypedTest, ParseZeroVersion) {
    auto v = this->parse("0.0.0");
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(0, v->major);
    EXPECT_EQ(0, v->minor);
    EXPECT_EQ(0, v->patch);
}

TYPED_TEST(SemVerTypedTest, ParseLargeNumbers) {
    auto v = this->parse("123.456.789");
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(123, v->major);
    EXPECT_EQ(456, v->minor);
    EXPECT_EQ(789, v->patch);
}

TYPED_TEST(SemVerTypedTest, ParseWithPreRelease) {
    auto v = this->parse("1.2.3-alpha");
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(1, v->major);
    EXPECT_EQ(2, v->minor);
    EXPECT_EQ(3, v->patch);
    ASSERT_EQ(1, v->preRelease.size());
    EXPECT_EQ(this->str("alpha"), v->preRelease[0]);
}

TYPED_TEST(SemVerTypedTest, ParseWithMultiplePreReleaseIdentifiers) {
    auto v = this->parse("1.0.0-alpha.1.beta.2");
    ASSERT_NE(nullptr, v);
    ASSERT_EQ(4, v->preRelease.size());
    EXPECT_EQ(this->str("alpha"), v->preRelease[0]);
    EXPECT_EQ(this->str("1"), v->preRelease[1]);
    EXPECT_EQ(this->str("beta"), v->preRelease[2]);
    EXPECT_EQ(this->str("2"), v->preRelease[3]);
}

TYPED_TEST(SemVerTypedTest, ParseWithBuildMetadata) {
    auto v = this->parse("1.2.3+build");
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(1, v->major);
    EXPECT_EQ(2, v->minor);
    EXPECT_EQ(3, v->patch);
    EXPECT_EQ(this->str("build"), v->buildMetadata);
}

TYPED_TEST(SemVerTypedTest, ParseWithMultipleBuildMetadataIdentifiers) {
    auto v = this->parse("1.0.0+20130313144700");
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(this->str("20130313144700"), v->buildMetadata);
}

TYPED_TEST(SemVerTypedTest, ParseWithPreReleaseAndBuildMetadata) {
    auto v = this->parse("1.2.3-rc.1+build.123");
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(1, v->major);
    EXPECT_EQ(2, v->minor);
    EXPECT_EQ(3, v->patch);
    ASSERT_EQ(2, v->preRelease.size());
    EXPECT_EQ(this->str("rc"), v->preRelease[0]);
    EXPECT_EQ(this->str("1"), v->preRelease[1]);
    EXPECT_EQ(this->str("build.123"), v->buildMetadata);
}

TYPED_TEST(SemVerTypedTest, ParsePreReleaseWithHyphens) {
    auto v = this->parse("1.0.0-alpha-beta");
    ASSERT_NE(nullptr, v);
    ASSERT_EQ(1, v->preRelease.size());
    EXPECT_EQ(this->str("alpha-beta"), v->preRelease[0]);
}

// ============================================================================
// Parse Tests - Non-Strict Mode
// ============================================================================

TYPED_TEST(SemVerTypedTest, ParseNonStrictTwoPartVersion) {
    auto v = this->parse("1.2", false);
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(1, v->major);
    EXPECT_EQ(2, v->minor);
    EXPECT_EQ(0, v->patch);
}

TYPED_TEST(SemVerTypedTest, RejectTwoPartVersionInStrictMode) {
    auto v = this->parse("1.2", true);
    EXPECT_EQ(nullptr, v);
}

// ============================================================================
// Parse Tests - Invalid Formats
// ============================================================================

TYPED_TEST(SemVerTypedTest, RejectSingleNumber) {
    auto v = this->parse("1");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectFourPartVersion) {
    auto v = this->parse("1.2.3.4");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectEmptyString) {
    auto v = this->parse("");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectNonNumericMajor) {
    auto v = this->parse("a.2.3");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectNonNumericMinor) {
    auto v = this->parse("1.b.3");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectNonNumericPatch) {
    auto v = this->parse("1.2.c");
    EXPECT_EQ(nullptr, v);
}

// ============================================================================
// Parse Tests - Leading Zeros
// ============================================================================

TYPED_TEST(SemVerTypedTest, RejectLeadingZeroInMajor) {
    auto v = this->parse("01.2.3");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectLeadingZeroInMinor) {
    auto v = this->parse("1.02.3");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectLeadingZeroInPatch) {
    auto v = this->parse("1.2.03");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectLeadingZeroInPreReleaseNumeric) {
    auto v = this->parse("1.2.3-01");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectLeadingZeroInPreReleaseIdentifier) {
    auto v = this->parse("1.2.3-alpha.01");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, AcceptZeroWithoutLeadingZero) {
    auto v = this->parse("0.0.0");
    ASSERT_NE(nullptr, v);
    EXPECT_EQ(0, v->major);
    EXPECT_EQ(0, v->minor);
    EXPECT_EQ(0, v->patch);
}

// ============================================================================
// Parse Tests - Invalid Characters
// ============================================================================

TYPED_TEST(SemVerTypedTest, RejectInvalidCharactersInPreRelease) {
    auto v = this->parse("1.2.3-alpha@beta");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectInvalidCharactersInBuildMetadata) {
    auto v = this->parse("1.2.3+build$123");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectSpacesInVersion) {
    auto v = this->parse("1.2.3 ");
    EXPECT_EQ(nullptr, v);
}

// ============================================================================
// Parse Tests - Empty Identifiers
// ============================================================================

TYPED_TEST(SemVerTypedTest, RejectEmptyPreRelease) {
    auto v = this->parse("1.2.3-");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectEmptyBuildMetadata) {
    auto v = this->parse("1.2.3+");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectEmptyPreReleaseIdentifier) {
    auto v = this->parse("1.2.3-alpha..beta");
    EXPECT_EQ(nullptr, v);
}

// ============================================================================
// Parse Tests - Overflow
// ============================================================================

TYPED_TEST(SemVerTypedTest, RejectMajorVersionOverflow) {
    auto v = this->parse("99999999999999999999.0.0");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectMinorVersionOverflow) {
    auto v = this->parse("0.99999999999999999999.0");
    EXPECT_EQ(nullptr, v);
}

TYPED_TEST(SemVerTypedTest, RejectPatchVersionOverflow) {
    auto v = this->parse("0.0.99999999999999999999");
    EXPECT_EQ(nullptr, v);
}

// ============================================================================
// Compare Tests - Major, Minor, Patch
// ============================================================================

TYPED_TEST(SemVerTypedTest, CompareMajorVersionGreater) {
    auto v1 = this->parse("2.0.0");
    auto v2 = this->parse("1.9.9");
    EXPECT_EQ(1, v1->compare(*v2));
    EXPECT_GT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, CompareMajorVersionLess) {
    auto v1 = this->parse("1.0.0");
    auto v2 = this->parse("2.0.0");
    EXPECT_EQ(-1, v1->compare(*v2));
    EXPECT_LT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, CompareMinorVersionGreater) {
    auto v1 = this->parse("1.2.0");
    auto v2 = this->parse("1.1.9");
    EXPECT_EQ(1, v1->compare(*v2));
    EXPECT_GT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, CompareMinorVersionLess) {
    auto v1 = this->parse("1.1.0");
    auto v2 = this->parse("1.2.0");
    EXPECT_EQ(-1, v1->compare(*v2));
    EXPECT_LT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, ComparePatchVersionGreater) {
    auto v1 = this->parse("1.0.2");
    auto v2 = this->parse("1.0.1");
    EXPECT_EQ(1, v1->compare(*v2));
    EXPECT_GT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, ComparePatchVersionLess) {
    auto v1 = this->parse("1.0.1");
    auto v2 = this->parse("1.0.2");
    EXPECT_EQ(-1, v1->compare(*v2));
    EXPECT_LT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, CompareEqual) {
    auto v1 = this->parse("1.2.3");
    auto v2 = this->parse("1.2.3");
    EXPECT_EQ(0, v1->compare(*v2));
    EXPECT_EQ(*v1, *v2);
}

// ============================================================================
// Compare Tests - Pre-release Precedence
// ============================================================================

TYPED_TEST(SemVerTypedTest, NormalVersionGreaterThanPreRelease) {
    auto v1 = this->parse("1.0.0");
    auto v2 = this->parse("1.0.0-alpha");
    EXPECT_EQ(1, v1->compare(*v2));
    EXPECT_GT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, PreReleaseLessThanNormalVersion) {
    auto v1 = this->parse("1.0.0-alpha");
    auto v2 = this->parse("1.0.0");
    EXPECT_EQ(-1, v1->compare(*v2));
    EXPECT_LT(*v1, *v2);
}

// ============================================================================
// Compare Tests - Pre-release Identifiers
// ============================================================================

TYPED_TEST(SemVerTypedTest, CompareNumericIdentifiers) {
    auto v1 = this->parse("1.0.0-2");
    auto v2 = this->parse("1.0.0-1");
    EXPECT_EQ(1, v1->compare(*v2));
    EXPECT_GT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, CompareNumericIdentifiersLess) {
    auto v1 = this->parse("1.0.0-1");
    auto v2 = this->parse("1.0.0-10");
    EXPECT_EQ(-1, v1->compare(*v2));
    EXPECT_LT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, CompareAlphanumericIdentifiers) {
    auto v1 = this->parse("1.0.0-beta");
    auto v2 = this->parse("1.0.0-alpha");
    EXPECT_EQ(1, v1->compare(*v2));
    EXPECT_GT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, CompareAlphanumericIdentifiersLess) {
    auto v1 = this->parse("1.0.0-alpha");
    auto v2 = this->parse("1.0.0-beta");
    EXPECT_EQ(-1, v1->compare(*v2));
    EXPECT_LT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, NumericIdentifierLessThanAlphanumeric) {
    auto v1 = this->parse("1.0.0-1");
    auto v2 = this->parse("1.0.0-alpha");
    EXPECT_EQ(-1, v1->compare(*v2));
    EXPECT_LT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, AlphanumericIdentifierGreaterThanNumeric) {
    auto v1 = this->parse("1.0.0-alpha");
    auto v2 = this->parse("1.0.0-1");
    EXPECT_EQ(1, v1->compare(*v2));
    EXPECT_GT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, ComparePreReleaseLength) {
    auto v1 = this->parse("1.0.0-alpha.1");
    auto v2 = this->parse("1.0.0-alpha");
    EXPECT_EQ(1, v1->compare(*v2));
    EXPECT_GT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, ComparePreReleaseLengthLess) {
    auto v1 = this->parse("1.0.0-alpha");
    auto v2 = this->parse("1.0.0-alpha.1");
    EXPECT_EQ(-1, v1->compare(*v2));
    EXPECT_LT(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, CompareComplexPreRelease) {
    auto v1 = this->parse("1.0.0-alpha.beta");
    auto v2 = this->parse("1.0.0-alpha.1");
    EXPECT_EQ(1, v1->compare(*v2));
    EXPECT_GT(*v1, *v2);
}

// ============================================================================
// Compare Tests - Build Metadata Ignored
// ============================================================================

TYPED_TEST(SemVerTypedTest, BuildMetadataIgnoredInComparison) {
    auto v1 = this->parse("1.0.0+build1");
    auto v2 = this->parse("1.0.0+build2");
    EXPECT_EQ(0, v1->compare(*v2));
    EXPECT_EQ(*v1, *v2);
}

TYPED_TEST(SemVerTypedTest, BuildMetadataIgnoredWithPreRelease) {
    auto v1 = this->parse("1.0.0-alpha+build1");
    auto v2 = this->parse("1.0.0-alpha+build2");
    EXPECT_EQ(0, v1->compare(*v2));
    EXPECT_EQ(*v1, *v2);
}

// ============================================================================
// Operator Tests
// ============================================================================

TYPED_TEST(SemVerTypedTest, LessThanOperator) {
    auto v1 = this->parse("1.0.0");
    auto v2 = this->parse("2.0.0");
    EXPECT_TRUE(*v1 < *v2);
    EXPECT_FALSE(*v2 < *v1);
}

TYPED_TEST(SemVerTypedTest, GreaterThanOperator) {
    auto v1 = this->parse("2.0.0");
    auto v2 = this->parse("1.0.0");
    EXPECT_TRUE(*v1 > *v2);
    EXPECT_FALSE(*v2 > *v1);
}

TYPED_TEST(SemVerTypedTest, EqualityOperator) {
    auto v1 = this->parse("1.2.3");
    auto v2 = this->parse("1.2.3");
    EXPECT_TRUE(*v1 == *v2);
}

TYPED_TEST(SemVerTypedTest, InequalityOperator) {
    auto v1 = this->parse("1.2.3");
    auto v2 = this->parse("1.2.4");
    EXPECT_TRUE(*v1 != *v2);
}

TYPED_TEST(SemVerTypedTest, LessOrEqualOperator) {
    auto v1 = this->parse("1.0.0");
    auto v2 = this->parse("2.0.0");
    auto v3 = this->parse("1.0.0");
    EXPECT_TRUE(*v1 <= *v2);
    EXPECT_TRUE(*v1 <= *v3);
    EXPECT_FALSE(*v2 <= *v1);
}

TYPED_TEST(SemVerTypedTest, GreaterOrEqualOperator) {
    auto v1 = this->parse("2.0.0");
    auto v2 = this->parse("1.0.0");
    auto v3 = this->parse("2.0.0");
    EXPECT_TRUE(*v1 >= *v2);
    EXPECT_TRUE(*v1 >= *v3);
    EXPECT_FALSE(*v2 >= *v1);
}

// ============================================================================
// SemVer Spec Examples (from https://semver.org/)
// ============================================================================

TYPED_TEST(SemVerTypedTest, ExampleVersionPrecedence) {
    auto v1 = this->parse("1.0.0-alpha");
    auto v2 = this->parse("1.0.0-alpha.1");
    auto v3 = this->parse("1.0.0-alpha.beta");
    auto v4 = this->parse("1.0.0-beta");
    auto v5 = this->parse("1.0.0-beta.2");
    auto v6 = this->parse("1.0.0-beta.11");
    auto v7 = this->parse("1.0.0-rc.1");
    auto v8 = this->parse("1.0.0");

    EXPECT_LT(*v1, *v2);
    EXPECT_LT(*v2, *v3);
    EXPECT_LT(*v3, *v4);
    EXPECT_LT(*v4, *v5);
    EXPECT_LT(*v5, *v6);
    EXPECT_LT(*v6, *v7);
    EXPECT_LT(*v7, *v8);
}