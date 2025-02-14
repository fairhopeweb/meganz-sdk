/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include <array>
#include <tuple>

#include <gtest/gtest.h>

#include <mega/base64.h>
#include <mega/filesystem.h>
#include <mega/utils.h>
#include "megafs.h"

#include <mega/db.h>
#include <mega/db/sqlite.h>
#include <mega/json.h>

TEST(utils, hashCombine_integer)
{
    size_t hash = 0;
    mega::hashCombine(hash, 42);
#ifdef _WIN32
    // MSVC's std::hash gives different values than that of gcc/clang
    ASSERT_EQ(sizeof(hash) == 4 ? 286246808ul : 10203658983813110072ull, hash);
#else
    ASSERT_EQ(2654435811ull, hash);
#endif
}

TEST(utils, readLines)
{
    static const std::string input =
        "\r"
        "\n"
        "     \r"
        "  a\r\n"
        "b\n"
        "c\r"
        "  d  \r"
        "     \n"
        "efg\n";
    static const std::vector<std::string> expected = {
        "  a",
        "b",
        "c",
        "  d  ",
        "efg"
    };

    std::vector<std::string> output;

    ASSERT_TRUE(::mega::readLines(input, output));
    ASSERT_EQ(output.size(), expected.size());
    ASSERT_TRUE(std::equal(expected.begin(), expected.end(), output.begin()));
}

TEST(Filesystem, EscapesReservedCharacters)
{
    using namespace mega;

    // All of these characters will be escaped.
    string name = "\\/:?\"<>|*";   // not % anymore (for now)

    // Generate expected result.
    ostringstream osstream;

    for (auto character : name)
    {
        osstream << "%"
                 << std::hex
                 << std::setfill('0')
                 << std::setw(2)
                 << +character;
    }

    // Use most restrictive escaping policy.
    FSACCESS_CLASS fsAccess;
    fsAccess.escapefsincompatible(&name, FS_UNKNOWN);

    // Was the string correctly escaped?
    ASSERT_EQ(name, osstream.str());
}

TEST(Filesystem, UnescapesEscapedCharacters)
{
    using namespace mega;

    FSACCESS_CLASS fsAccess;

    // All of these characters will be escaped.
    string name = "%\\/:?\"<>|*";
    fsAccess.escapefsincompatible(&name, FS_UNKNOWN);

    // Everything will be unescaped except for control characters.
    fsAccess.unescapefsincompatible(&name);

    // Was the string correctly unescaped?
    ASSERT_STREQ(name.c_str(), "%\\/:?\"<>|*");
}


TEST(CharacterSet, IterateUtf8)
{
    using ::mega::unicodeCodepointIterator;

    // Single code-unit.
    {
        auto it = unicodeCodepointIterator("abc");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), 'a');
        EXPECT_EQ(it.get(), 'b');
        EXPECT_EQ(it.get(), 'c');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), '\0');
    }

    // Multiple code-unit.
    {
        auto it = unicodeCodepointIterator("q\xf0\x90\x80\x80r");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), 'q');
        EXPECT_EQ(it.get(), 0x10000);
        EXPECT_EQ(it.get(), 'r');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), '\0');
    }
}

TEST(CharacterSet, IterateUtf16)
{
    using mega::unicodeCodepointIterator;

    // Single code-unit.
    {
        auto it = unicodeCodepointIterator(L"abc");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), L'a');
        EXPECT_EQ(it.get(), L'b');
        EXPECT_EQ(it.get(), L'c');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), L'\0');
    }

    // Multiple code-unit.
    {
        auto it = unicodeCodepointIterator(L"q\xd800\xdc00r");

        EXPECT_FALSE(it.end());
        EXPECT_EQ(it.get(), L'q');
        EXPECT_EQ(it.get(), 0x10000);
        EXPECT_EQ(it.get(), L'r');
        EXPECT_TRUE(it.end());
        EXPECT_EQ(it.get(), L'\0');
    }
}

using namespace mega;
using namespace std;

// Disambiguate between Microsoft's FileSystemType.
using ::mega::FileSystemType;

class ComparatorTest
  : public ::testing::Test
{
public:
    template<typename T, typename U>
    int compare(const T& lhs, const U& rhs) const
    {
        return compareUtf(lhs, true, rhs, true, false);
    }

    template<typename T, typename U>
    int ciCompare(const T& lhs, const U& rhs) const
    {
        return compareUtf(lhs, true, rhs, true, true);
    }

    LocalPath fromAbsPath(const string& s)
    {
        return LocalPath::fromAbsolutePath(s);
    }

    LocalPath fromRelPath(const string& s)
    {
        return LocalPath::fromRelativePath(s);
    }

}; // ComparatorTest

TEST_F(ComparatorTest, CompareLocalPaths)
{
    LocalPath lhs;
    LocalPath rhs;

    // Case insensitive
    {
        // Make sure basic characters are uppercased.
        lhs = fromRelPath("abc");
        rhs = fromRelPath("ABC");

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
        EXPECT_EQ(ciCompare(rhs, lhs), 0);

        // Make sure comparison invariants are not violated.
        lhs = fromRelPath("abc");
        rhs = fromRelPath("ABCD");

        EXPECT_LT(ciCompare(lhs, rhs), 0);
        EXPECT_GT(ciCompare(rhs, lhs), 0);

        // Make sure escapes are decoded.
        lhs = fromRelPath("a%30b");
        rhs = fromRelPath("A0B");

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
        EXPECT_EQ(ciCompare(rhs, lhs), 0);

        // Make sure decoded characters are uppercased.
        lhs = fromRelPath("%61%62%63");
        rhs = fromRelPath("ABC");

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
        EXPECT_EQ(ciCompare(rhs, lhs), 0);

        // Invalid escapes are left as-is.
        lhs = fromRelPath("a%qb%");
        rhs = fromRelPath("A%qB%");

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
        EXPECT_EQ(ciCompare(rhs, lhs), 0);
    }

    // Case sensitive
    {
        // Basic comparison.
        lhs = fromRelPath("abc");

        EXPECT_EQ(compare(lhs, lhs), 0);

        // Make sure characters are not uppercased.
        rhs = fromRelPath("ABC");

        EXPECT_NE(compare(lhs, rhs), 0);
        EXPECT_NE(compare(rhs, lhs), 0);

        // Make sure comparison invariants are not violated.
        lhs = fromRelPath("abc");
        rhs = fromRelPath("abcd");

        EXPECT_LT(compare(lhs, rhs), 0);
        EXPECT_GT(compare(rhs, lhs), 0);

        // Make sure escapes are decoded.
        lhs = fromRelPath("a%30b");
        rhs = fromRelPath("a0b");

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        // Invalid escapes are left as-is.
        lhs = fromRelPath("a%qb%");

        EXPECT_EQ(compare(lhs, lhs), 0);

#ifdef _WIN32
        // Non-UNC prefixes should be skipped.
        lhs = fromAbsPath("\\\\?\\C:\\");
        rhs = fromAbsPath("C:\\");

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        lhs = fromAbsPath("\\\\.\\C:\\");
        rhs = fromAbsPath("C:\\");

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        //// Prefixes should only be removed from absolute paths.
        //lhs = fromAbsPath("\\\\?\\X");
        //rhs = fromAbsPath("X");

        //EXPECT_NE(compare(lhs, rhs), 0);
        //EXPECT_NE(compare(rhs, lhs), 0);
#endif // _WIN32
    }

    // Filesystem-specific
    {
        lhs = fromRelPath("a\7%30b%31c");
        rhs = fromRelPath("A%070B1C");

    }
}

TEST_F(ComparatorTest, CompareLocalPathAgainstString)
{
    LocalPath lhs;
    string rhs;

    // Case insensitive
    {
        // Simple comparison.
        lhs = fromRelPath("abc");
        rhs = "ABC";

        EXPECT_EQ(ciCompare(lhs, rhs), 0);

        // Invariants.
        lhs = fromRelPath("abc");
        rhs = "abcd";

        EXPECT_LT(ciCompare(lhs, rhs), 0);

        lhs = fromRelPath("abcd");
        rhs = "abc";

        EXPECT_GT(ciCompare(lhs, rhs), 0);

        // All local escapes are decoded.
        lhs = fromRelPath("a%30b%31c");
        rhs = "A0b1C";

        EXPECT_EQ(ciCompare(lhs, rhs), 0);

        // Escapes are uppercased.
        lhs = fromRelPath("%61%62%63");
        rhs = "ABC";

        EXPECT_EQ(ciCompare(lhs, rhs), 0);

        // Invalid escapes are left as-is.
        lhs = fromRelPath("a%qb%");
        rhs = "A%QB%";

        EXPECT_EQ(ciCompare(lhs, rhs), 0);
    }

    // Case sensitive
    {
        // Simple comparison.
        lhs = fromRelPath("abc");
        rhs = "abc";

        EXPECT_EQ(compare(lhs, rhs), 0);

        // Invariants.
        rhs = "abcd";

        EXPECT_LT(compare(lhs, rhs), 0);

        lhs = fromRelPath("abcd");
        rhs = "abc";

        EXPECT_GT(compare(lhs, rhs), 0);

        // All local escapes are decoded.
        lhs = fromRelPath("a%30b%31c");
        rhs = "a0b1c";

        EXPECT_EQ(compare(lhs, rhs), 0);

        // Invalid escapes left as-is.
        lhs = fromRelPath("a%qb%r");
        rhs = "a%qb%r";

        EXPECT_EQ(compare(lhs, rhs), 0);

#ifdef _WIN32
        // Non-UNC prefixes should be skipped.
        lhs = fromAbsPath("\\\\?\\C:\\");
        rhs = "C:\\";

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        lhs = fromAbsPath("\\\\.\\C:\\");
        rhs = "C:\\";

        EXPECT_EQ(compare(lhs, rhs), 0);
        EXPECT_EQ(compare(rhs, lhs), 0);

        //// Prefixes should only be removed from absolute paths.
        //lhs = fromAbsPath("\\\\?\\X");
        //rhs = "X";

        //EXPECT_NE(compare(lhs, rhs), 0);
        //EXPECT_NE(compare(rhs, lhs), 0);
#endif // _WIN32
    }

    // Filesystem-specific
    {
        lhs = fromRelPath("a\7%30b%31c");
        rhs = "A%070B1C";

    }
}

TEST(Conversion, HexVal)
{
    // Decimal [0-9]
    for (int i = 0x30; i < 0x3a; ++i)
    {
        EXPECT_EQ(hexval(i), i - 0x30);
    }

    // Lowercase hexadecimal [a-f]
    for (int i = 0x41; i < 0x47; ++i)
    {
        EXPECT_EQ(hexval(i), i - 0x37);
    }

    // Uppercase hexadeimcal [A-F]
    for (int i = 0x61; i < 0x67; ++i)
    {
        EXPECT_EQ(hexval(i), i - 0x57);
    }
}

TEST(URLCodec, Unescape)
{
    string input = "a%4a%4Bc";
    string output;

    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "aJKc");
}

TEST(URLCodec, UnescapeInvalidEscape)
{
    string input;
    string output;

    // First character is invalid.
    input = "a%qbc";
    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "a%qbc");

    // Second character is invalid.
    input = "a%bqc";
    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "a%bqc");
}

TEST(URLCodec, UnescapeShortEscape)
{
    string input;
    string output;

    // No hex digits.
    input = "a%";
    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "a%");

    // Single hex digit.
    input = "a%a";
    URLCodec::unescape(&input, &output);
    EXPECT_EQ(output, "a%a");
}


TEST(Filesystem, isContainingPathOf)
{
    using namespace mega;

#ifdef _WIN32
#define SEP "\\"
#else // _WIN32
#define SEP "/"
#endif // ! _WIN32

    LocalPath lhs;
    LocalPath rhs;
    size_t pos;

    // lhs does not contain rhs.
    constexpr const size_t sentinel = std::numeric_limits<size_t>::max();
    pos = sentinel;
    lhs = LocalPath::fromRelativePath("a" SEP "b");
    rhs = LocalPath::fromRelativePath("a" SEP "c");

    EXPECT_FALSE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, sentinel);

    // lhs does not contain rhs.
    // they do, however, share a common prefix.
    pos = sentinel;
    lhs = LocalPath::fromRelativePath("a");
    rhs = LocalPath::fromRelativePath("ab");

    EXPECT_FALSE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, sentinel);

    // lhs contains rhs.
    // no trailing separator.
    pos = sentinel;
    lhs = LocalPath::fromRelativePath("a");
    rhs = LocalPath::fromRelativePath("a" SEP "b");

    EXPECT_TRUE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, 2u);

    // trailing separator.
    pos = sentinel;
    lhs = LocalPath::fromRelativePath("a" SEP);
    rhs = LocalPath::fromRelativePath("a" SEP "b");

    EXPECT_TRUE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, 2u);

    // lhs contains itself.
    pos = sentinel;
    lhs = LocalPath::fromRelativePath("a" SEP "b");

    EXPECT_TRUE(lhs.isContainingPathOf(lhs, &pos));
    EXPECT_EQ(pos, 3u);

#ifdef _WIN32
    // case insensitive.
    pos = sentinel;
    lhs = LocalPath::fromRelativePath("a" SEP "B");
    rhs = LocalPath::fromRelativePath("A" SEP "b");

    EXPECT_TRUE(lhs.isContainingPathOf(rhs, &pos));
    EXPECT_EQ(pos, 3u);
#endif // _WIN32

#undef SEP
}


TEST(Filesystem, isReservedName)
{
    using namespace mega;

    bool expected = false;

#ifdef _WIN32
    expected = true;
#endif // _WIN32

    // Representative examples.
    static const string reserved[] = {"AUX", "com1", "LPT4"};

    for (auto& r : reserved)
    {
        EXPECT_EQ(isReservedName(r, FILENODE),   expected);
        EXPECT_EQ(isReservedName(r, FOLDERNODE), expected);
    }

    EXPECT_EQ(isReservedName("a.", FILENODE),   false);
    EXPECT_EQ(isReservedName("a.", FOLDERNODE), expected);
}

class SqliteDBTest
  : public ::testing::Test
{
public:
        SqliteDBTest()
          : Test()
          , fsAccess()
          , name("test")
          , rng()
          , rootPath(LocalPath::fromAbsolutePath("."))
        {
            // Get the current path.
            bool result = fsAccess.cwd(rootPath);
            if (!result)
                assert(result);

            // Create temporary DB root path.
            rootPath.appendWithSeparator(
                LocalPath::fromRelativePath("db"), false);

            // Make sure our root path is clear.
            fsAccess.emptydirlocal(rootPath);
            fsAccess.rmdirlocal(rootPath);

            // Create root path.
            result = fsAccess.mkdirlocal(rootPath, false, true);
            if (!result)
                assert(result);
        }

        ~SqliteDBTest()
        {
            // Remove temporary root path.
            fsAccess.emptydirlocal(rootPath);

            bool result = fsAccess.rmdirlocal(rootPath);
            if (!result)
                assert(result);
        }

        FSACCESS_CLASS fsAccess;
        string name;
        PrnGen rng;
        LocalPath rootPath;
}; // SqliteDBTest

TEST_F(SqliteDBTest, CreateCurrent)
{
    SqliteDbAccess dbAccess(rootPath);

    // Assume databases are in legacy format until proven otherwise.
    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::LEGACY_DB_VERSION);

    // Create a new database.
    DbTablePtr dbTable(dbAccess.openTableWithNodes(rng, fsAccess, name));

    // Was the database created successfully?
    ASSERT_TRUE(!!dbTable);

    // New databases should not be in the legacy format.
    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::DB_VERSION);

}

TEST_F(SqliteDBTest, OpenCurrent)
{
    // Create a dummy database.
    {
        SqliteDbAccess dbAccess(rootPath);

        EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::LEGACY_DB_VERSION);

        DbTablePtr dbTable(dbAccess.openTableWithNodes(rng, fsAccess, name));
        ASSERT_TRUE(!!dbTable);

        EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::DB_VERSION);
    }

    // Open the database.
    SqliteDbAccess dbAccess(rootPath);

    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::LEGACY_DB_VERSION);

    DbTablePtr dbTable(dbAccess.openTableWithNodes(rng, fsAccess, name));
    EXPECT_TRUE(!!dbTable);

    EXPECT_EQ(dbAccess.currentDbVersion, DbAccess::DB_VERSION);
}

TEST_F(SqliteDBTest, ProbeCurrent)
{
    SqliteDbAccess dbAccess(rootPath);

    // Create dummy database.
    {
        auto dbFile =
          dbAccess.databasePath(fsAccess,
                                name,
                                DbAccess::DB_VERSION);

        auto fileAccess = fsAccess.newfileaccess(false);
        EXPECT_TRUE(fileAccess->fopen(dbFile, false, true));
    }

    EXPECT_TRUE(dbAccess.probe(fsAccess, name));
}

TEST_F(SqliteDBTest, ProbeLegacy)
{
    SqliteDbAccess dbAccess(rootPath);

    // Create dummy database.
    {
        auto dbFile =
          dbAccess.databasePath(fsAccess,
                                name,
                                DbAccess::LEGACY_DB_VERSION);

        auto fileAccess = fsAccess.newfileaccess(false);
        EXPECT_TRUE(fileAccess->fopen(dbFile, false, true));
    }

    EXPECT_TRUE(dbAccess.probe(fsAccess, name));
}

TEST_F(SqliteDBTest, ProbeNone)
{
    SqliteDbAccess dbAccess(rootPath);
    EXPECT_FALSE(dbAccess.probe(fsAccess, name));
}


TEST_F(SqliteDBTest, RootPath)
{
    SqliteDbAccess dbAccess(rootPath);
    EXPECT_EQ(dbAccess.rootPath(), rootPath);
}

#ifdef WIN32
#define SEP "\\"
#else // WIN32
#define SEP "/"
#endif // ! WIN32

TEST(LocalPath, AppendWithSeparator)
{
    LocalPath source;
    LocalPath target;

    // Doesn't add a separator if the target is empty.
    source = LocalPath::fromRelativePath("a");
    target.appendWithSeparator(source, false);

    EXPECT_EQ(target.toPath(false), "a");

    // Doesn't add a separator if the source begins with one.
    source = LocalPath::fromRelativePath(SEP "b");
    target = LocalPath::fromRelativePath("a");

    target.appendWithSeparator(source, true);
    EXPECT_EQ(target.toPath(false), "a" SEP "b");

    // Doesn't add a separator if the target ends with one.
    source = LocalPath::fromRelativePath("b");
    target = LocalPath::fromRelativePath("a" SEP);

    target.appendWithSeparator(source, true);
    EXPECT_EQ(target.toPath(false), "a" SEP "b");

    // Adds a separator when:
    // - source doesn't begin with one.
    // - target doesn't end with one.
    target = LocalPath::fromRelativePath("a");

    target.appendWithSeparator(source, true);
    EXPECT_EQ(target.toPath(false), "a" SEP "b");
}

TEST(LocalPath, PrependWithSeparator)
{
    LocalPath source;
    LocalPath target;

    // No separator if target is empty.
    source = LocalPath::fromRelativePath("b");

    target.prependWithSeparator(source);
    EXPECT_EQ(target.toPath(false), "b");

    // No separator if target begins with separator.
    target = LocalPath::fromRelativePath(SEP "a");

    target.prependWithSeparator(source);
    EXPECT_EQ(target.toPath(false), "b" SEP "a");

    // No separator if source ends with separator.
    source = LocalPath::fromRelativePath("b" SEP);
    target = LocalPath::fromRelativePath("a");

    target.prependWithSeparator(source);
    EXPECT_EQ(target.toPath(false), "b" SEP "a");
}

#undef SEP

TEST(JSONWriter, arg_stringWithEscapes)
{
    JSONWriter writer;
    writer.arg_stringWithEscapes("ke", "\"\\");
    EXPECT_EQ(writer.getstring(), "\"ke\":\"\\\"\\\\\"");
}

TEST(JSONWriter, escape)
{
    class Writer
      : public JSONWriter
    {
    public:
        using JSONWriter::escape;
    };

    Writer writer;
    string input = "\"\\";
    string expected = "\\\"\\\\";

    EXPECT_EQ(writer.escape(input.c_str(), input.size()), expected);
}

TEST(JSON, stripWhitespace)
{
    auto input = string(" a\rb\n c\r{\"a\":\"q\\r \\\" s\"\n} x y\n z\n");
    auto expected = string("abc{\"a\":\"q\\r \\\" s\"}xyz");
    auto computed = JSON::stripWhitespace(input);

    ASSERT_EQ(computed, expected);

    input = "{\"a\":\"bcde";
    expected = "{\"a\":\"";
    computed = JSON::stripWhitespace(input);

    ASSERT_EQ(computed, expected);
}

TEST(Utils, replace_char)
{
    ASSERT_EQ(Utils::replace(string(""), '*', '@'), "");
    ASSERT_EQ(Utils::replace(string("*"), '*', '@'), "@");
    ASSERT_EQ(Utils::replace(string("**"), '*', '@'), "@@");
    ASSERT_EQ(Utils::replace(string("*aa"), '*', '@'), "@aa");
    ASSERT_EQ(Utils::replace(string("*aa*bb*"), '*', '@'), "@aa@bb@");
    ASSERT_EQ(Utils::replace(string("sd*"), '*', '@'), "sd@");
    ASSERT_EQ(Utils::replace(string("*aa**bb*"), '*', '@'), "@aa@@bb@");
}

TEST(Utils, replace_string)
{
    ASSERT_EQ(Utils::replace(string(""), "*", "@"), "");
    ASSERT_EQ(Utils::replace(string("*"), "*", "@"), "@");
    ASSERT_EQ(Utils::replace(string("**"), "*", "@"), "@@");
    ASSERT_EQ(Utils::replace(string("*aa"), "*", "@"), "@aa");
    ASSERT_EQ(Utils::replace(string("*aa*bb*"), "*", "@"), "@aa@bb@");
    ASSERT_EQ(Utils::replace(string("sd*"), "*", "@"), "sd@");
    ASSERT_EQ(Utils::replace(string("*aa**bb*"), "*", "@"), "@aa@@bb@");
    ASSERT_EQ(Utils::replace(string("*aa**bb*"), "*", "@"), "@aa@@bb@");

    ASSERT_EQ(Utils::replace(string(""), "", "@"), "");
    ASSERT_EQ(Utils::replace(string("abc"), "", "@"), "abc");
}

TEST(RemotePath, nextPathComponent)
{
    // Absolute path.
    {
        RemotePath path("/a/b/");

        RemotePath component;
        size_t index = 0;

        ASSERT_TRUE(path.nextPathComponent(index, component));
        ASSERT_EQ(component, "a");

        ASSERT_TRUE(path.nextPathComponent(index, component));
        ASSERT_EQ(component, "b");

        ASSERT_FALSE(path.nextPathComponent(index, component));
        ASSERT_TRUE(component.empty());

        // Sanity.
        path = RemotePath("/");

        index = 0;

        ASSERT_FALSE(path.nextPathComponent(index, component));
        ASSERT_TRUE(component.empty());
    }

    // Relative path.
    {
        RemotePath path("a/b/");

        RemotePath component;
        size_t index = 0;

        ASSERT_TRUE(path.nextPathComponent(index, component));
        ASSERT_EQ(component, "a");

        ASSERT_TRUE(path.nextPathComponent(index, component));
        ASSERT_EQ(component, "b");

        ASSERT_FALSE(path.nextPathComponent(index, component));
        ASSERT_TRUE(component.empty());

        // Sanity.
        path = RemotePath("");

        index = 0;

        ASSERT_FALSE(path.nextPathComponent(index, component));
        ASSERT_TRUE(component.empty());
    }
}

class TooLongNameTest
    : public ::testing::Test
{
public:
    TooLongNameTest()
      : Test()
      , mPrefixName(LocalPath::fromRelativePath("d"))
      , mPrefixPath()
    {
    }

    LocalPath Append(const LocalPath& prefix, const string& name) const
    {
        LocalPath path = prefix;

        path.appendWithSeparator(
          LocalPath::fromRelativeName(name, mFsAccess, FS_UNKNOWN),
          false);

        return path;
    }

    LocalPath AppendLongName(const LocalPath& prefix, char character) const
    {
        // Representative limit.
        //
        // True limit depends on specific filesystem.
        constexpr size_t MAX_COMPONENT_LENGTH = 255;

        string name(MAX_COMPONENT_LENGTH + 1, character);

        return Append(prefix, name);
    }

    bool CreateDummyFile(const LocalPath& path)
    {
        ::mega::byte data = 0x21;

        auto fileAccess = mFsAccess.newfileaccess(false);

        return fileAccess->fopen(path, false, true)
               && fileAccess->fwrite(&data, 1, 0);
    }

    void SetUp() override
    {
        // Flag should initially be clear.
        ASSERT_FALSE(mFsAccess.target_name_too_long);

        // Retrieve the current working directory.
        ASSERT_TRUE(mFsAccess.cwd(mPrefixPath));

        // Compute absolute path to "container" directory.
        mPrefixPath.appendWithSeparator(mPrefixName, false);

        // Remove container directory.
        mFsAccess.emptydirlocal(mPrefixPath);
        mFsAccess.rmdirlocal(mPrefixPath);

        // Create container directory.
        ASSERT_TRUE(mFsAccess.mkdirlocal(mPrefixPath, false, true));
    }

    void TearDown() override
    {
        // Destroy container directory.
        mFsAccess.emptydirlocal(mPrefixPath);
        mFsAccess.rmdirlocal(mPrefixPath);
    }

    FSACCESS_CLASS mFsAccess;
    LocalPath mPrefixName;
    LocalPath mPrefixPath;
}; // TooLongNameTest

TEST_F(TooLongNameTest, Copy)
{
    // Absolute
    {
        auto source = Append(mPrefixPath, "s");
        auto target = AppendLongName(mPrefixPath, 'u');

        ASSERT_TRUE(CreateDummyFile(source));

        ASSERT_FALSE(mFsAccess.copylocal(source, target, 0));
        ASSERT_TRUE(mFsAccess.target_name_too_long);

        // Legitimate "bad path" error should clear the flag.
        target = Append(mPrefixPath, "u");
        target = Append(target, "v");

        ASSERT_FALSE(mFsAccess.copylocal(source, target, 0));
        ASSERT_FALSE(mFsAccess.target_name_too_long);
    }
}

TEST_F(TooLongNameTest, CreateDirectory)
{
    // Absolute
    {
        auto path = AppendLongName(mPrefixPath, 'x');

        ASSERT_FALSE(mFsAccess.mkdirlocal(path, false, true));
        ASSERT_TRUE(mFsAccess.target_name_too_long);

        // A legitimate "bad path" error should clear the flag.
        path = Append(mPrefixPath, "x");
        path = Append(path, "y");

        ASSERT_FALSE(mFsAccess.mkdirlocal(path, false, true));
        ASSERT_FALSE(mFsAccess.target_name_too_long);
    }
}

TEST_F(TooLongNameTest, Rename)
{
    // Absolute
    {
        auto source = Append(mPrefixPath, "q");
        auto target = AppendLongName(mPrefixPath, 'r');

        ASSERT_TRUE(mFsAccess.mkdirlocal(source, false, true));

        ASSERT_FALSE(mFsAccess.renamelocal(source, target, false));
        ASSERT_TRUE(mFsAccess.target_name_too_long);

        // Legitimate "bad path" error should clear the flag.
        target = Append(mPrefixPath, "u");
        target = Append(target, "v");

        ASSERT_FALSE(mFsAccess.renamelocal(source, target, false));
        ASSERT_FALSE(mFsAccess.target_name_too_long);
    }
}
