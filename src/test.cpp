#include "vfs.h"
#include "procvfs.h"

#include "sqlite3.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <unordered_map>

using ::testing::_;
using ::testing::Invoke;

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

void logSqliteError(void *pArg, int iErrCode, const char *zMsg) { fprintf(stderr, "(%d) %s\n", iErrCode, zMsg); }
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
}

TEST(MyTest, MyTest)
{
  Mock mock;
  ON_CALL(mock, cppCallback(_)).WillByDefault(Invoke(logCppSqliteArguments));

  ASSERT_EQ(SQLITE_OK, sqlite3_config(SQLITE_CONFIG_LOG, logSqliteError, nullptr));

//  ASSERT_EQ(SQLITE_OK, sqlite3_vfs_register(sqlite3_demovfs(), 1));
  ASSERT_EQ(SQLITE_OK, procvfs_init());

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

