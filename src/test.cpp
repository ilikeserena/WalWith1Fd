#include "vfs.h"
#include "procvfs.h"
#include "ProxyVfs.h"

#include "sqlite3.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace {

const char *dbFile = "test.db";

class Database
{
 public:
  Database(const char *dbFile) : db() { EXPECT_EQ(SQLITE_OK, sqlite3_open(dbFile, &db)); }
  ~Database() { sqlite3_close(db); }
  operator sqlite3 *() const { return db; }
  sqlite3 *db;
};

void logSqliteError(void * /*pArg*/, int iErrCode, const char *zMsg) { fprintf(stderr, "(%d) %s\n", iErrCode, zMsg); }
class Mock
{
 public:
  MOCK_CONST_METHOD1(cppCallback, int(const std::unordered_map<std::string, std::string> &keyValues));

  static int callback(void *data, int argc, char **argv, char **azColName)
  {
    Mock &mock = *(Mock *)data;
    std::unordered_map<std::string, std::string> keyValues;
    for (int i = 0; i < argc; ++i)
    {
      keyValues[azColName[i]] = argv[i];
    }
    return mock.cppCallback(keyValues);
  }
};

int logCppSqliteArguments(const std::unordered_map<std::string, std::string> &keyValues)
{
  for (auto keyValue : keyValues)
  {
    printf("%s='%s' ", keyValue.first.c_str(), keyValue.second.c_str());
  }
  printf("\n");
  return 0;
}

class MockVfs : public ::testing::Mock
{
public:
    MOCK_METHOD5(xOpen, int(sqlite3_vfs* vfs, const char *zName, sqlite3_file* file, int flags, int *pOutFlags));
    MOCK_METHOD3(xDelete, int(sqlite3_vfs* vfs, const char *zName, int syncDir));
};

MockVfs cppMockVfs;

struct sqlite3_vfs sqliteMockVfs =
{
    3, // int iVersion;            /* Structure version number (currently 3) */
    sizeof(sqlite3_file), //int szOsFile;            /* Size of subclassed sqlite3_file */
    0, // int mxPathname;          /* Maximum file pathname length */
    nullptr, //sqlite3_vfs *pNext;      /* Next registered VFS */
    "mockvfs", // const char *zName;       /* Name of this virtual file system */
    nullptr, // void *pAppData;          /* Pointer to application-specific data */
    [](sqlite3_vfs* vfs, const char *zName, sqlite3_file* file, int flags, int *pOutFlags)
    { return cppMockVfs.xOpen(vfs, zName, file, flags, pOutFlags); },
    [](sqlite3_vfs* vfs, const char *zName, int syncDir)
    { return cppMockVfs.xDelete(vfs, zName, syncDir); }
};

}

TEST(MyTest, UnitTest)
{
    sqlite3_vfs_register(&sqliteMockVfs, 1);
    ProxyVfs scopedProxyVfs;
    auto* proxyVfs = sqlite3_vfs_find("proxyvfs");
    ASSERT_NE(nullptr, proxyVfs);
    std::vector<std::uint8_t> fileBuffer(proxyVfs->szOsFile, 0);
    sqlite3_file* testFile = reinterpret_cast<sqlite3_file*>(fileBuffer.data());
    EXPECT_CALL(cppMockVfs, xOpen(&sqliteMockVfs, "zName", _, 2, nullptr)).WillOnce(Return(1));
    ASSERT_EQ(1, proxyVfs->xOpen(proxyVfs, "zName", testFile, 2, nullptr));
}

TEST(MyTest, IntegrationTest)
{
  Mock mock;
  ON_CALL(mock, cppCallback(_)).WillByDefault(Invoke(logCppSqliteArguments));

  ASSERT_EQ(SQLITE_OK, sqlite3_config(SQLITE_CONFIG_LOG, logSqliteError, nullptr));

  sqlite3_vfs * defaultVfs = sqlite3_vfs_find(nullptr);
  ASSERT_NE(nullptr, defaultVfs);
  ASSERT_STREQ("unix", defaultVfs->zName);
//  ASSERT_EQ(SQLITE_OK, sqlite3_vfs_register(sqlite3_demovfs(), 1));
  //ASSERT_EQ(SQLITE_OK, sqlite3_vfs_register(sqlite3_vfs_find("unix-excl"), 1));
  //ASSERT_EQ(SQLITE_OK, procvfs_init());
  ProxyVfs proxyVfs;

  printf("db1\n");
  Database db1(dbFile);
  auto expectedWalMode = std::unordered_map<std::string, std::string>{{"journal_mode", "wal"}};
  EXPECT_CALL(mock, cppCallback(expectedWalMode));
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db1, "PRAGMA journal_mode=WAL;", Mock::callback, &mock, nullptr));
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db1, "DROP TABLE IF EXISTS COMPANY;", nullptr, nullptr, nullptr));
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db1,
                                    "CREATE TABLE COMPANY("
                                    "ID INT PRIMARY KEY     NOT NULL,"
                                    "NAME           TEXT    NOT NULL,"
                                    "AGE            INT     NOT NULL,"
                                    "ADDRESS        CHAR(50),"
                                    "SALARY         REAL);",
                                    nullptr, nullptr, nullptr));
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db1,
                                    "INSERT INTO COMPANY (ID,NAME,AGE,ADDRESS,SALARY) "
                                    "VALUES (1, 'Paul', 32, 'California', 20000.00 );",
                                    nullptr, nullptr, nullptr));

  printf("db2\n");
  Database db2(dbFile);
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db2, "BEGIN;", nullptr, nullptr, nullptr));
  EXPECT_CALL(mock, cppCallback(_)).Times(1);
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db2, "SELECT * FROM COMPANY;", Mock::callback, &mock, nullptr));

  printf("db1\n");
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db1,
                                    "INSERT INTO COMPANY (ID,NAME,AGE,ADDRESS,SALARY) "
                                    "VALUES (2, 'Allen', 25, 'Texas', 15000.00 ); "
                                    "INSERT INTO COMPANY (ID,NAME,AGE,ADDRESS,SALARY) "
                                    "VALUES (3, 'Teddy', 23, 'Norway', 20000.00 ); "
                                    "INSERT INTO COMPANY (ID,NAME,AGE,ADDRESS,SALARY) "
                                    "VALUES (4, 'Mark', 25, 'Rich-Mond ', 65000.00 );",
                                    nullptr, nullptr, nullptr));

  printf("db3\n");
  Database db3(dbFile);
  EXPECT_CALL(mock, cppCallback(_)).Times(4);
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db3, "SELECT * FROM COMPANY;", Mock::callback, &mock, nullptr));

  printf("db2\n");
  EXPECT_CALL(mock, cppCallback(_)).Times(1);
  ASSERT_EQ(SQLITE_OK, sqlite3_exec(db2, "SELECT * FROM COMPANY;", Mock::callback, &mock, nullptr));
}

