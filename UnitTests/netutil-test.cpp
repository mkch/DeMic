#include "pch.h"
#include "../WebRemote/NetUtil.h"

TEST(NetUtilTest, SplitHostPort_ValidInput) {
	net_util::HostPort result;
	net_util::SplitHostPort("example.com:8080", &result);
	EXPECT_EQ("example.com", result.Host);
	EXPECT_EQ("8080", result.Port);
}

TEST(NetUtilTest, AcceptLanguageMatcher_ValidInput) {
	std::vector<std::string> supportedLanguages = { "en-US", "zh-CN", "fr-FR" };
	EXPECT_EQ("zh-CN", net_util::AcceptLanguageMatcher::Match("zh-CN,zh;q=0.9,en;q=0.8", supportedLanguages).value_or(""));
	EXPECT_EQ("zh-CN", net_util::AcceptLanguageMatcher::Match("zh-CN,zh;q=0.9,en;q=0.8", supportedLanguages).value_or(""));
	EXPECT_EQ("en-US", net_util::AcceptLanguageMatcher::Match("en-US,en;q=0.9,zh-CN;q=0.8", supportedLanguages).value_or(""));
	EXPECT_EQ("fr-FR", net_util::AcceptLanguageMatcher::Match("fr-FR,fr;q=0.9,en;q=0.8", supportedLanguages).value_or(""));
	EXPECT_EQ("", net_util::AcceptLanguageMatcher::Match("de-DE,de;q=0.9", supportedLanguages).value_or(""));

	EXPECT_EQ("en-US", net_util::AcceptLanguageMatcher::Match("zh-CN,zh;q=0.9,en;q=0.8", {"en-US"}).value_or(""));
}