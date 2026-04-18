#include "pch.h"
#include "../Util.h"
#include <vector>

// Test FromUTF8 with C-string
TEST(UtilTest, FromUTF8_CString_Simple) {
    const char8_t* input = u8"Hello";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"Hello", result);
}

TEST(UtilTest, FromUTF8_CString_Empty) {
    const char8_t* input = u8"";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"", result);
}

TEST(UtilTest, FromUTF8_CString_Nullptr) {
    EXPECT_THROW(FromUTF8(nullptr), std::invalid_argument);
}

TEST(UtilTest, FromUTF8_CString_ASCII) {
    const char8_t* input = u8"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", result);
}

TEST(UtilTest, FromUTF8_CString_Chinese) {
    const char8_t* input = u8"你好世界";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"你好世界", result);
}

TEST(UtilTest, FromUTF8_CString_Japanese) {
    const char8_t* input = u8"こんにちは";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"こんにちは", result);
}

TEST(UtilTest, FromUTF8_CString_Emoji) {
    const char8_t* input = u8"Hello 😊";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"Hello \U0001F60A", result);
}

TEST(UtilTest, FromUTF8_CString_Mixed) {
    const char8_t* input = u8"English中文日本語😊";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"English中文日本語\U0001F60A", result);
}

TEST(UtilTest, FromUTF8_CString_SpecialChars) {
    const char8_t* input = u8"!@#$%^&*()_+-=[]{}|;':\",./<>?";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"!@#$%^&*()_+-=[]{}|;':\",./<>?", result);
}

TEST(UtilTest, FromUTF8_CString_WithExplicitLength) {
    const char8_t* input = u8"Hello World";
    std::wstring result = FromUTF8(input, 5);
    EXPECT_EQ(L"Hello", result);
}

TEST(UtilTest, FromUTF8_CString_ZeroLength) {
    const char8_t* input = u8"Hello";
    std::wstring result = FromUTF8(input, 0);
    EXPECT_EQ(L"", result);
}

TEST(UtilTest, FromUTF8_CString_WithEmbeddedNull) {
    const char8_t data[] = {'H', 'e', 'l', 'l', 'o', '\0', 'W', 'o', 'r', 'l', 'd', '\0'};
    std::wstring result = FromUTF8(data, 11);
    EXPECT_EQ(11, result.size());
    EXPECT_EQ(L'H', result[0]);
    EXPECT_EQ(L'e', result[1]);
    EXPECT_EQ(L'\0', result[5]);
    EXPECT_EQ(L'W', result[6]);
    EXPECT_EQ(L'd', result[10]);
}

// Test FromUTF8 with std::u8string
TEST(UtilTest, FromUTF8_StdString_Simple) {
   std::u8string input = u8"Hello";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"Hello", result);
}

TEST(UtilTest, FromUTF8_StdString_Empty) {
   std::u8string input = u8"";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"", result);
}

TEST(UtilTest, FromUTF8_StdString_Chinese) {
   std::u8string input = u8"测试字符串";
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(L"测试字符串", result);
}

TEST(UtilTest, FromUTF8_StdString_WithEmbeddedNull) {
   std::u8string input(10, '\0');
    input[0] = 'A';
    input[5] = 'B';
    input[9] = 'C';
    std::wstring result = FromUTF8(input);
    EXPECT_EQ(10, result.size());
    EXPECT_EQ(L'A', result[0]);
    EXPECT_EQ(L'\0', result[1]);
    EXPECT_EQ(L'B', result[5]);
    EXPECT_EQ(L'C', result[9]);
}

// Test ToUTF8 with C-string
TEST(UtilTest, ToUTF8_CString_Simple) {
    const wchar_t* input = L"Hello";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8"Hello"),  (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_Empty) {
    const wchar_t* input = L"";
   std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8""),  (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_Nullptr) {
    EXPECT_THROW(ToUTF8(nullptr), std::invalid_argument);
}

TEST(UtilTest, ToUTF8_CString_ASCII) {
    const wchar_t* input = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"),  (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_Chinese) {
    const wchar_t* input = L"你好世界";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8"你好世界"), (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_Japanese) {
    const wchar_t* input = L"こんにちは";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8"こんにちは"), (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_Emoji) {
    const wchar_t* input = L"Hello \U0001F60A";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8"Hello \U0001F60A"), (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_Mixed) {
    const wchar_t* input = L"English中文日本語\U0001F60A";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8"English中文日本語\U0001F60A"), (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_SpecialChars) {
    const wchar_t* input = L"!@#$%^&*()_+-=[]{}|;':\",./<>?";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8"!@#$%^&*()_+-=[]{}|;':\",./<>?"), (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_WithExplicitLength) {
    const wchar_t* input = L"Hello World";
    std::u8string result = ToUTF8(input, 5);
    EXPECT_EQ(std::string((const char*)u8"Hello"),  (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_ZeroLength) {
    const wchar_t* input = L"Hello";
    std::u8string result = ToUTF8(input, 0);
    EXPECT_EQ(std::string((const char*)u8""),  (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_CString_WithEmbeddedNull) {
    const wchar_t data[] = {L'H', L'e', L'l', L'l', L'o', L'\0', L'W', L'o', L'r', L'l', L'd', L'\0'};
    std::u8string result = ToUTF8(data, 11);
    EXPECT_EQ(11, result.size());
    EXPECT_EQ(std::string((const char*)u8"H"), (const char*)result.substr(0, 1).c_str());
    EXPECT_EQ(std::string((const char*)u8"e"), (const char*)result.substr(1, 1).c_str());
    EXPECT_EQ(std::string((const char*)u8"\0"), (const char*)result.substr(5, 1).c_str());
    EXPECT_EQ(std::string((const char*)u8"W"), (const char*)result.substr(6, 1).c_str());
    EXPECT_EQ(std::string((const char*)u8"d"), (const char*)result.substr(10, 1).c_str());
}

// Test ToUTF8 with std::wstring
TEST(UtilTest, ToUTF8_StdWString_Simple) {
    std::wstring input = L"Hello";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8"Hello"),  (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_StdWString_Empty) {
    std::wstring input = L"";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8""), (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_StdWString_Chinese) {
    std::wstring input = L"测试字符串";
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(std::string((const char*)u8"测试字符串"), (const char*)result.c_str());
}

TEST(UtilTest, ToUTF8_StdWString_WithEmbeddedNull) {
    std::wstring input(10, L'\0');
    input[0] = L'A';
    input[5] = L'B';
    input[9] = L'C';
    std::u8string result = ToUTF8(input);
    EXPECT_EQ(10, result.size());
    EXPECT_EQ(std::string((const char*)u8"A"), (const char*)result.substr(0, 1).c_str());
    EXPECT_EQ(std::string((const char*)u8"\0"), (const char*)result.substr(1, 1).c_str());
    EXPECT_EQ(std::string((const char*)u8"B"), (const char*)result.substr(5, 1).c_str());
    EXPECT_EQ(std::string((const char*)u8"C"), (const char*)result.substr(9, 1).c_str());
}

// Round-trip tests
TEST(UtilTest, RoundTrip_ASCII) {
    const char8_t* original = u8"Hello World";
    std::wstring wide = FromUTF8(original);
    std::u8string back = ToUTF8(wide);
    EXPECT_EQ(std::string((const char*)original), (const char*)back.c_str());
}

TEST(UtilTest, RoundTrip_Chinese) {
    const char8_t* original = u8"你好世界，这是一个测试";
    std::wstring wide = FromUTF8(original);
    std::u8string back = ToUTF8(wide);
    EXPECT_EQ(std::string((const char*)original), (const char*)back.c_str());
}

TEST(UtilTest, RoundTrip_Mixed) {
    const char8_t* original = u8"English中文日本語??Русский";
    std::wstring wide = FromUTF8(original);
    std::u8string back = ToUTF8(wide);
    EXPECT_EQ(std::string((const char*)original), (const char*)back.c_str());
}

TEST(UtilTest, RoundTrip_Emoji) {
    const char8_t* original = u8"✔❌😊";
    std::wstring wide = FromUTF8(original);
    std::u8string back = ToUTF8(wide);
    EXPECT_EQ(std::string((const char*)original), (const char*)back.c_str());
}

TEST(UtilTest, RoundTrip_WithEmbeddedNull) {
    std::u8string original(20, '\0');
    original[0] = 'A';
    original[10] = 'B';
    original[19] = 'C';
    std::wstring wide = FromUTF8(original);
    std::u8string back = ToUTF8(wide);
    EXPECT_EQ(std::string((const char*)original.c_str()), (const char*)back.c_str());
}

// Long string test
TEST(UtilTest, LongString_UTF8) {
    std::u8string input;
    for (int i = 0; i < 1000; i++) {
        input += u8"测试";
    }
    std::wstring wide = FromUTF8(input);
    std::u8string back = ToUTF8(wide);
    EXPECT_EQ(std::string((const char*)input.c_str()), (const char*)back.c_str());
    EXPECT_EQ(2000, wide.size());
}

#include <wininet.h>
TEST(UtilTest, Win32Error) {
    Win32Error error(123);
    EXPECT_EQ(123, error.Code());

	auto msgA = Win32Error::GetMessageA(ERROR_PATH_NOT_FOUND/*3*/);
	EXPECT_TRUE(msgA.length() > std::strlen("3: "));
	auto msgW = Win32Error::GetMessageW(ERROR_PATH_NOT_FOUND);
    EXPECT_TRUE(msgA.length() > std::strlen("3: "));
	auto notExistMsgA = Win32Error::GetMessageA(4294967295/*0xFFFFFFFF*/);
    EXPECT_EQ(notExistMsgA, "4294967295: ");

    Win32Error pathNotFound(ERROR_PATH_NOT_FOUND);
	EXPECT_EQ(pathNotFound.Code(), ERROR_PATH_NOT_FOUND);
	EXPECT_EQ(pathNotFound.what(), msgA);

	auto hWininet = LoadLibraryW(L"wininet.dll");
	EXPECT_NE(hWininet, HMODULE(0));
    Win32Error internetTimeout(ERROR_INTERNET_TIMEOUT/*12002*/, hWininet);
    EXPECT_EQ(internetTimeout.Code(), ERROR_INTERNET_TIMEOUT);
    auto internetTimeoutMsgA = Win32Error::GetMessageA(ERROR_INTERNET_TIMEOUT, hWininet);
	EXPECT_TRUE(internetTimeoutMsgA.length() > std::strlen("12002: "));
    EXPECT_EQ(internetTimeout.what(), internetTimeoutMsgA);
    if (hWininet) {
        FreeLibrary(hWininet);
    }
}

#include "resource.h"

TEST(UtilTest, LoadModuleResource) {
    auto resource = LoadModuleResource<std::byte>(NULL, L"TEST_RESOURCE", MAKEINTRESOURCEW(IDR_TEST_RESOURCE1));
	auto data = std::string_view(reinterpret_cast<const char*>(resource.data()), resource.size());
	EXPECT_EQ("ABCD", data);
}