#include "ui/stage_excluded.h"

#include <iostream>
#include <stdexcept>

using namespace atrium;

int main()
{
    int failures = 0;
    auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    auto run = [&](const char* name, auto test) {
        try {
            test();
            std::cout << "PASS: " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    };

    run("an executable name excludes every install path", [&] {
        require(StageAppExcluded({L"wechat.exe"}, L"d:\\apps\\wechat\\wechat.exe"), "the file name must match a full app key");
        require(StageAppExcluded({L"wechat.exe"}, L"c:\\program files\\wechat\\wechat.exe"), "another install path must match too");
    });
    run("AUMIDs match by prefix", [&] {
        require(StageAppExcluded({L"microsoft.screensketch_"}, L"microsoft.screensketch_8wekyb3d8bbwe!app"), "a package prefix must match its AUMID");
    });
    run("window keys match their app", [&] {
        require(StageAppExcluded({L"wechat.exe"}, L"d:\\apps\\wechat\\wechat.exe|2a4"), "a per-window key must match by its app key");
        require(StageAppExcluded({L"microsoft.screensketch_"}, L"microsoft.screensketch_8wekyb3d8bbwe!app|2a4"),
            "a packaged window key must match its package prefix");
    });
    run("case does not matter", [&] {
        require(StageAppExcluded({L"WeChat.EXE"}, L"D:\\Apps\\WeChat\\wechat.exe"), "entries and app keys compare without case");
    });
    run("a full path entry matches exactly", [&] {
        require(StageAppExcluded({L"d:\\apps\\tool.exe"}, L"d:\\apps\\tool.exe"), "a full key must match");
    });
    run("unrelated apps are not excluded", [&] {
        require(!StageAppExcluded({L"wechat.exe"}, L"d:\\apps\\notepad.exe"), "no substring or unrelated match");
        require(!StageAppExcluded({}, L"d:\\apps\\wechat.exe"), "an empty list excludes nothing");
        require(!StageAppExcluded({L""}, L"d:\\apps\\wechat.exe"), "an empty entry is ignored");
    });
    run("the defaults cover the Windows screenshot tools", [&] {
        const auto defaults = DefaultStageExcluded();
        require(StageAppExcluded(defaults, L"c:\\windows\\system32\\snippingtool.exe"), "Windows Snipping Tool");
        require(StageAppExcluded(defaults, L"c:\\windows\\system32\\screenclippinghost.exe"), "Screen Clipping Host");
        require(StageAppExcluded(defaults, L"microsoft.screensketch_8wekyb3d8bbwe!app"), "packaged Snipping Tool");
        require(!StageAppExcluded(defaults, L"d:\\apps\\wechat\\wechat.exe"), "other screenshot tools are opt-in");
        require(!StageAppExcluded(defaults, L"c:\\windows\\notepad.exe"), "ordinary apps stay managed");
    });
    return failures ? 1 : 0;
}
