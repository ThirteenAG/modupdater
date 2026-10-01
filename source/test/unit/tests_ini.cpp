#include "stdafx.h"
#include "test.h"
#include "archive.h"
#include "extract.h"
#include "inimerge.h"
#include "string_funcs.h"
#include "common/test_util.h"
// mINI with MINI_CASE_SENSITIVE in this executable, like in FusionFix and the other modules that link the library
#include <IniReader.h>

using namespace mu;

namespace
{
    // "Key = value" with the comment at the column FusionFix uses
    std::string Setting(const std::string& setting, const std::string& comment = {})
    {
        if (comment.empty())
            return setting + "\n";
        return setting + std::string(setting.size() < 46 ? 46 - setting.size() : 1, ' ') + comment + "\n";
    }
}

TEST_CASE(Ini_MergeKeepsTheNewLayout)
{
    auto oldText =
        "[MAIN]\n" + Setting("RecoilFix = 0", "// make recoil behavior the same") +
        "[BudgetedIV]\n" + Setting("VehicleBudget = 260000000", "// used to avoid \"Taxi Bug\"") + Setting("PedBudget = 60000000", "// may cause issues") +
        "[SHADOWS]\n" + Setting("CascadeBlendSize = 0.25", "// controls the size | [0.0; 1.0]") +
        "[UPDATE]\n" + Setting("Url = https://example.com/a//b");
    auto newText =
        "[MAIN]\n" + Setting("RecoilFix = 1", "// make recoil behavior the same as controller") + Setting("AimingZoomFix = 1", "// new key") +
        "\n[BudgetedIV]\n" + Setting("VehicleBudget = 0", "// used to avoid \"Taxi Bug\", may cause vehicle audio issue") + Setting("PedBudget = 0", "// may cause issues") +
        "[SHADOWS]\n" + Setting("CascadeBlendSize = 0.1", "// controls the size of the cascade blending region | [0.0; 1.0]") +
        "[UPDATE]\n" + Setting("Url = https://example.com/new");

    // the comments of the new file stay aligned, "//" in a value is no comment, ';' inside a comment is no comment either
    auto expected =
        "[MAIN]\n" + Setting("RecoilFix = 0", "// make recoil behavior the same as controller") + Setting("AimingZoomFix = 1", "// new key") +
        "\n[BudgetedIV]\n" + Setting("VehicleBudget = 260000000", "// used to avoid \"Taxi Bug\", may cause vehicle audio issue") + Setting("PedBudget = 60000000", "// may cause issues") +
        "[SHADOWS]\n" + Setting("CascadeBlendSize = 0.25", "// controls the size of the cascade blending region | [0.0; 1.0]") +
        "[UPDATE]\n" + Setting("Url = https://example.com/a//b");

    CHECK_EQ(MergeIniText(oldText, newText), expected);
    CHECK_EQ(MergeIniText(newText, newText), newText);
}

TEST_CASE(Ini_MergeIgnoresCase)
{
    CHECK_EQ(MergeIniText("[main]\nrecoilfix = 0\n", "[MAIN]\nRecoilFix = 1 // c\n"), std::string("[MAIN]\nRecoilFix = 0 // c\n"));
    CHECK_EQ(MergeIniText("[Main]\nA = 1\n[MAIN]\nB = 2\n", "[MAIN]\nA = 0\nB = 0\n"), std::string("[MAIN]\nA = 1\nB = 2\n"));
}

TEST_CASE(Ini_MergeRepairsDuplicatedSections)
{
    // What the updater of FusionFix 5.1.0 and 5.1.1 wrote (issue 1611): the new file with its defaults, then the
    // settings of the user in lowercase. The next update gets the settings back and the lowercase copies go away.
    auto defaults = "[MAIN]\n" + Setting("RecoilFix = 1", "// make recoil behavior the same as controller") +
        "\n[BudgetedIV]\n" + Setting("VehicleBudget = 0", "// used to avoid \"Taxi Bug\"") + Setting("PedBudget = 0", "// may cause issues");
    auto broken = defaults +
        "\n[main]\nrecoilfix = 0\n = \n"
        "\n[budgetediv]\nvehiclebudget = 260000000\npedbudget = 60000000\n = \n";
    auto repaired = "[MAIN]\n" + Setting("RecoilFix = 0", "// make recoil behavior the same as controller") +
        "\n[BudgetedIV]\n" + Setting("VehicleBudget = 260000000", "// used to avoid \"Taxi Bug\"") + Setting("PedBudget = 60000000", "// may cause issues");

    CHECK_EQ(MergeIniText(broken, defaults), repaired);
}

TEST_CASE(Ini_MergeKeepsUserKeysAndSections)
{
    // keys of the user at the end of their section, the last of duplicates, sections the new file does not have at the end
    CHECK_EQ(MergeIniText("[MAIN]\nA = 1\nMine = 5\n\n[OLD] // comment\nX = 1\nX = 2\n", "[MAIN]\nA = 0\n\n[NEW]\nB = 0\n"),
        std::string("[MAIN]\nA = 1\nMine = 5\n\n[NEW]\nB = 0\n\n[OLD] // comment\nX = 2\n"));

    // keys before the first section are only merged with keys before the first section
    CHECK_EQ(MergeIniText("A = 1\n[S]\nB = 2\n", "A = 0\n[S]\nA = 0\nB = 0\n"), std::string("A = 1\n[S]\nA = 0\nB = 2\n"));
}

TEST_CASE(Ini_MergeKeepsLineBreaksAndBom)
{
    CHECK_EQ(MergeIniText("[MAIN]\nA = 1\n", "\xEF\xBB\xBF[MAIN]\r\nA = 0\r\n"), std::string("\xEF\xBB\xBF[MAIN]\r\nA = 1\r\n"));
    CHECK_EQ(MergeIniText("\xEF\xBB\xBF[MAIN]\r\nA = 1\r\n", "[MAIN]\nA = 0"), std::string("[MAIN]\nA = 1"));
}

TEST_CASE(Ini_MergeValues)
{
    auto merged = MergeIniText("[MAIN]\nKey =\nToken = abc\nColor=#FF0000\nList = a, b ; old comment\n",
        "[MAIN]\nKey = default ; comment\nToken =\nColor=#000000\nList = x\n");
    // an empty value keeps the comment in place, "Key =" gets a space, "Key=value" stays compact
    CHECK_EQ(merged, "[MAIN]\nKey =" + std::string(9, ' ') + "; comment\nToken = abc\nColor=#FF0000\nList = a, b\n");
}

TEST_CASE(Ini_MergeInAModuleThatUsesIniReader)
{
    // FusionFix uses CIniReader as well, merging must not depend on the variant of mINI that ends up in the module
    test::TempDir dir(L"inireader");
    auto settings = dir / L"Other.ini";
    test::WriteFile(settings, "[MAIN]\nValue = 1\n");
    {
        CIniReader ini(settings);
        ini.WriteInteger("MAIN", "Value", ini.ReadInteger("MAIN", "Value", 0) + 1, true);
    }
    CHECK_EQ(CIniReader(settings).ReadInteger("MAIN", "Value", 0), 2);

    auto oldIni = "[MAIN]\n" + Setting("RecoilFix = 0", "// comment") + "\n[BudgetedIV]\n" + Setting("VehicleBudget = 260000000", "// comment");
    auto newIni = "[MAIN]\n" + Setting("RecoilFix = 1", "// comment") + Setting("AimingZoomFix = 1", "// comment") +
        "\n[BudgetedIV]\n" + Setting("VehicleBudget = 0", "// comment");
    auto game = dir / L"game";
    test::WriteFile(game / L"plugins" / L"Mod.ini", oldIni);
    auto zip = dir / L"Mod.zip";
    REQUIRE(test::CreateZip(zip, { { "plugins/Mod.ini", newIni } }));
    zip::Reader reader;
    REQUIRE(reader.Open(zip));
    ExtractReport report;
    CHECK(ExtractArchive(reader, game, {}, {}, report));
    CHECK_EQ(report.merged, 1);
    CHECK_EQ(test::ReadFile(game / L"plugins" / L"Mod.ini"),
        "[MAIN]\n" + Setting("RecoilFix = 0", "// comment") + Setting("AimingZoomFix = 1", "// comment") +
        "\n[BudgetedIV]\n" + Setting("VehicleBudget = 260000000", "// comment"));
}
