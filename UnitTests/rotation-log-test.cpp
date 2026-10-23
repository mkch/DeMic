#include "pch.h"
#include "../Log.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <iterator>
#include <string>

// Globals of the application that Log.cpp depends on.
std::wstring appTitle;
HWND mainWindow = NULL;

namespace {

namespace fs = std::filesystem;

class RotationLogTest : public ::testing::Test {
protected:
	fs::path dir;
	fs::path logFilePath;

	void SetUp() override {
		static size_t index = 0;
		dir = fs::temp_directory_path() / std::format(L"DeMicRotationLogTest-{}-{}", ::GetCurrentProcessId(), index++);
		fs::remove_all(dir);
		fs::create_directories(dir);
		logFilePath = dir / L"DeMic.log";
	}

	void TearDown() override {
		fs::remove_all(dir);
	}

	fs::path BackupPath(size_t n) const {
		return dir / std::format(L"DeMic-{}.log", n);
	}

	// Creates a file of the given size, starting with the given content padded with 'x'.
	static void MakeFile(const fs::path& path, size_t size, const std::string& content) {
		std::string data = content;
		data.resize(size, 'x');
		std::ofstream file(path, std::ios::trunc | std::ios::binary);
		file.write(data.data(), static_cast<std::streamsize>(data.size()));
	}

	static std::string ReadAll(const fs::path& path) {
		std::ifstream file(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

	void ExpectFile(const fs::path& path, size_t size, const std::string& content) {
		ASSERT_TRUE(fs::exists(path)) << path.string();
		EXPECT_EQ(size, fs::file_size(path)) << path.string();
		EXPECT_EQ(content, ReadAll(path).substr(0, content.size())) << path.string();
	}
};

} // namespace

TEST_F(RotationLogTest, CreatesLogFileWhenMissing) {
	auto stream = OpenRotatingLogFile(logFilePath.wstring(), 1024, 3);
	ASSERT_TRUE(stream.is_open());
	stream << "hello";
	stream.close();
	EXPECT_EQ("hello", ReadAll(logFilePath));
	EXPECT_FALSE(fs::exists(BackupPath(0)));
}

TEST_F(RotationLogTest, AppendsWhenBelowLimit) {
	MakeFile(logFilePath, 10, "OLD");
	auto stream = OpenRotatingLogFile(logFilePath.wstring(), 1024, 3);
	ASSERT_TRUE(stream.is_open());
	stream << "NEW";
	stream.close();
	EXPECT_EQ(13u, fs::file_size(logFilePath));
	EXPECT_EQ("OLD", ReadAll(logFilePath).substr(0, 3));
	EXPECT_EQ("NEW", ReadAll(logFilePath).substr(10));
	EXPECT_FALSE(fs::exists(BackupPath(0)));
}

TEST_F(RotationLogTest, RotatesWhenExceedingLimit) {
	MakeFile(logFilePath, 2048, "OLD");
	auto stream = OpenRotatingLogFile(logFilePath.wstring(), 1024, 3);
	ASSERT_TRUE(stream.is_open());
	stream << "NEW";
	stream.close();
	ExpectFile(BackupPath(0), 2048, "OLD");
	EXPECT_EQ("NEW", ReadAll(logFilePath));
	EXPECT_FALSE(fs::exists(BackupPath(1)));
}

TEST_F(RotationLogTest, RotatesRepeatedlyAndDropsOldest) {
	for (int round = 1; round <= 4; round++) {
		MakeFile(logFilePath, 2048, std::format("C{}", round));
		auto stream = OpenRotatingLogFile(logFilePath.wstring(), 1024, 3);
		ASSERT_TRUE(stream.is_open()) << "round " << round;
	}
	ExpectFile(BackupPath(0), 2048, "C2");
	ExpectFile(BackupPath(1), 2048, "C3");
	ExpectFile(BackupPath(2), 2048, "C4");
	EXPECT_FALSE(fs::exists(BackupPath(3)));
	EXPECT_EQ(0u, fs::file_size(logFilePath));
}

TEST_F(RotationLogTest, ClosesGapsInBackupNumbers) {
	MakeFile(BackupPath(2), 2048, "GAP2");
	MakeFile(logFilePath, 2048, "CURRENT");
	auto stream = OpenRotatingLogFile(logFilePath.wstring(), 1024, 3);
	ASSERT_TRUE(stream.is_open());
	ExpectFile(BackupPath(0), 2048, "GAP2");
	ExpectFile(BackupPath(1), 2048, "CURRENT");
	EXPECT_FALSE(fs::exists(BackupPath(2)));
	EXPECT_EQ(0u, fs::file_size(logFilePath));
}

TEST_F(RotationLogTest, DoesNotRotateWhenMaxBackupFilesIsZero) {
	MakeFile(logFilePath, 2048, "BIG");
	auto stream = OpenRotatingLogFile(logFilePath.wstring(), 1024, 0);
	ASSERT_TRUE(stream.is_open());
	stream << "MORE";
	stream.close();
	ExpectFile(logFilePath, 2052, "BIG");
	EXPECT_FALSE(fs::exists(BackupPath(0)));
}

TEST_F(RotationLogTest, ReturnsFailedStreamWhenRotatingLogFileFails) {
	MakeFile(logFilePath, 2048, "OLD");
	// The backup file cannot be replaced by the log file.
	fs::create_directory(BackupPath(0));

	auto stream = OpenRotatingLogFile(logFilePath.wstring(), 1024, 1);

	EXPECT_FALSE(stream.is_open());
	EXPECT_FALSE(static_cast<bool>(stream)) << "failbit is expected, like a failed open()";
	stream << "x";
	EXPECT_FALSE(stream.good());
	ExpectFile(logFilePath, 2048, "OLD"); // The log file is left untouched.
}

TEST_F(RotationLogTest, ReturnsFailedStreamWhenShiftingBackupsFails) {
	MakeFile(BackupPath(2), 2048, "GAP2");
	MakeFile(logFilePath, 2048, "OLD");
	// Opening the backup file without FILE_SHARE_DELETE makes renaming it fail.
	const HANDLE handle = ::CreateFileW(BackupPath(2).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	ASSERT_NE(INVALID_HANDLE_VALUE, handle);

	auto stream = OpenRotatingLogFile(logFilePath.wstring(), 1024, 3);
	::CloseHandle(handle);

	EXPECT_FALSE(stream.is_open());
	EXPECT_FALSE(static_cast<bool>(stream));
	ExpectFile(logFilePath, 2048, "OLD");
	ExpectFile(BackupPath(2), 2048, "GAP2");
}

TEST_F(RotationLogTest, ReturnsFailedStreamWhenLogFileIsNotAFile) {
	fs::create_directory(logFilePath);
	auto stream = OpenRotatingLogFile(logFilePath.wstring(), 1024, 3);
	EXPECT_FALSE(stream.is_open());
	EXPECT_FALSE(static_cast<bool>(stream));
	EXPECT_TRUE(fs::is_directory(logFilePath));
}
