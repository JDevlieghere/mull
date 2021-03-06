#include "SQLiteReporter.h"

#include "Logger.h"
#include "Result.h"
#include "TestResult.h"

#include "MutationOperators/MutationOperator.h"

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <sqlite3.h>
#include <sstream>
#include <string>
#include <sys/param.h>
#include <unistd.h>

using namespace Mutang;
using namespace llvm;

static void createTables(sqlite3 *database);

static void assume(bool condition, const char *assumption) {
  if (condition) {
    return;
  }

  Logger::warn() << "Assumption failed: " << assumption << '\n';
}

void sqlite_exec(sqlite3 *database, const char *sql) {
  char *errorMessage;
  int result = sqlite3_exec(database,
                            sql,
                            nullptr,
                            nullptr,
                            &errorMessage);
  if (result != SQLITE_OK) {
    Logger::error() << "Cannot execute " << sql << '\n';
    Logger::error() << "Reason: '" << errorMessage << "'\n";
    Logger::error() << "Shutting down\n";
    exit(18);
  }
}

SQLiteReporter::SQLiteReporter() {
  char wd[MAXPATHLEN] = { 0 };
  getwd(wd);
  std::string currentDirectory(wd);

  time_t ct;
  time(&ct);
  std::string currentTime = std::to_string(ct);

  std::string databasePath = currentDirectory + "/" + currentTime + ".sqlite";

  this->databasePath = databasePath;
}

std::string Mutang::SQLiteReporter::getDatabasePath() {
  return databasePath;
}

void Mutang::SQLiteReporter::reportResults(const std::unique_ptr<Result> &result) {
  std::string databasePath = getDatabasePath();

  sqlite3 *database;
  sqlite3_open(databasePath.c_str(), &database);

  createTables(database);

  for (auto &testResult : result->getTestResults()) {
    std::string testID = testResult->getTestName();

    ExecutionResult testExecutionResult = testResult->getOriginalTestResult();
    std::string insertResultSQL = std::string("INSERT INTO execution_result VALUES (")
    + "'" + std::to_string(testExecutionResult.Status) + "',"
    + "'" + std::to_string(testExecutionResult.RunningTime) + "',"
    + "'" + testExecutionResult.stdoutOutput + "',"
    + "'" + testExecutionResult.stderrOutput + "');";
    sqlite_exec(database, insertResultSQL.c_str());
    int testResultID = sqlite3_last_insert_rowid(database);

    std::string insertTestSQL = std::string("INSERT INTO test VALUES (")
    + "'" + testID + "',"
    + "'" + std::to_string(testResultID) + "');";
    sqlite_exec(database, insertTestSQL.c_str());

    for (auto &mutation : testResult->getMutationResults()) {
      std::vector<std::string> callerPath = result.get()->calculateCallerPath(mutation.get());

      std::string callerPathAsString = getCallerPathAsString(callerPath);

      /// Mutation Point
      auto mutationPoint = mutation->getMutationPoint();
      Instruction *instruction = dyn_cast<Instruction>(mutationPoint->getOriginalValue());
      std::string insertMutationPointSQL = std::string("INSERT OR IGNORE INTO mutation_point VALUES (")
      + "'" + mutationPoint->getOperator()->uniqueID() + "',"
      + "'" + instruction->getParent()->getParent()->getParent()->getModuleIdentifier() + "',"
      + "'" + instruction->getParent()->getParent()->getName().str() + "',"
      + "'" + std::to_string(mutationPoint->getAddress().getFnIndex()) + "',"
      + "'" + std::to_string(mutationPoint->getAddress().getBBIndex()) + "',"
      + "'" + std::to_string(mutationPoint->getAddress().getIIndex()) + "',"
      + "'" + instruction->getDebugLoc()->getFilename().str() + "',"
      + "'" + std::to_string(instruction->getDebugLoc()->getLine()) + "',"
      + "'" + std::to_string(instruction->getDebugLoc()->getColumn()) + "',"
      + "'" + callerPathAsString + "',"
      + "'" + mutationPoint->getUniqueIdentifier() + "'"+
      + ");";

      sqlite_exec(database, insertMutationPointSQL.c_str());
      std::string mutationPointID = mutationPoint->getUniqueIdentifier();

      {
        std::string function;
        llvm::raw_string_ostream f_ostream(function);
        instruction->getFunction()->print(f_ostream);

        std::string basicBlock;
        llvm::raw_string_ostream bb_ostream(basicBlock);
        instruction->getParent()->print(bb_ostream);

        std::string instr;
        llvm::raw_string_ostream i_ostream(instr);
        instruction->print(i_ostream);

        std::string insertMutationPointDebugSQL = std::string("INSERT OR IGNORE INTO mutation_point_debug VALUES (")
        + "'" + instruction->getDebugLoc()->getFilename().str() + "',"
        + "'" + std::to_string(instruction->getDebugLoc()->getLine()) + "',"
        + "'" + std::to_string(instruction->getDebugLoc()->getColumn()) + "',"
        + "'" + f_ostream.str() + "',"
        + "'" + bb_ostream.str() + "',"
        + "'" + i_ostream.str() + "',"
        + "'" + mutationPoint->getUniqueIdentifier() + "'"+
        + ");";

        sqlite3_stmt *statement = NULL;
        int statement_prepare_result =
        sqlite3_prepare_v2(database,
                           insertMutationPointDebugSQL.c_str(),
                           insertMutationPointDebugSQL.size(),
                           &statement,
                           NULL);

        assume(statement_prepare_result == SQLITE_OK, "SQLite error: Expected finalize of debug mutation point statement to succeed.");

        int insertion_result = sqlite3_step(statement);
        assume(insertion_result == SQLITE_DONE, "SQLite error: Expected insertion of debug mutation point to succeed.");

        int finalize_result = sqlite3_finalize(statement);
        assume(finalize_result == SQLITE_OK, "SQLite error: Expected finalize of debug mutation point statement to succeed.");
      }

      /// Execution result
      ExecutionResult mutationExecutionResult = mutation->getExecutionResult();
      std::string insertMutationExecutionResultSQL = std::string("INSERT INTO execution_result VALUES (")
      + "'" + std::to_string(mutationExecutionResult.Status) + "',"
      + "'" + std::to_string(mutationExecutionResult.RunningTime) + "',"
      + "" + "?" + ","
      + "" + "?" + ");";

      sqlite3_stmt *execution_result_statement = NULL;
      int execution_result_statement_prepare_result =
        sqlite3_prepare_v2(database,
                           insertMutationExecutionResultSQL.c_str(),
                           insertMutationExecutionResultSQL.size(),
                           &execution_result_statement,
                           NULL);

      assume(execution_result_statement_prepare_result == SQLITE_OK,
             "SQLite error: Expected preparation of execution result statement to succeed.");

      if (execution_result_statement_prepare_result != SQLITE_OK) {
        Logger::error() << insertMutationExecutionResultSQL << '\n';
        Logger::error() << sqlite3_errmsg(database) << '\n';
      }

      sqlite3_bind_text(execution_result_statement, 1, mutationExecutionResult.stdoutOutput.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(execution_result_statement, 2, mutationExecutionResult.stderrOutput.c_str(), -1, SQLITE_TRANSIENT);

      int execution_result_insertion_result = sqlite3_step(execution_result_statement);
      assume(execution_result_insertion_result == SQLITE_DONE,
             "SQLite error: Expected insertion of execution result to succeed.");

      int execution_result_statement_finalize_result
        = sqlite3_finalize(execution_result_statement);
      assume(execution_result_statement_finalize_result == SQLITE_OK,
             "SQLite error: Expected finalize of execution result statement to succeed.");

      int mutationExecutionResultID = sqlite3_last_insert_rowid(database);
      std::string insertMutationResultSQL = std::string("INSERT INTO mutation_result VALUES (")
          + "'" + std::to_string(mutationExecutionResultID) + "',"
          + "'" + testID + "',"
          + "'" + mutationPointID + "',"
          + "'" + std::to_string(mutation->getMutationDistance()) + "');";

      sqlite_exec(database, insertMutationResultSQL.c_str());
    }
  }

  sqlite3_close(database);

  outs() << "Results can be found at '" << databasePath << "'\n";
}

#pragma mark -

std::string SQLiteReporter::getCallerPathAsString(const std::vector<std::string> &callerPath) {
  std::string callerPathAsString;

  unsigned int indentation = 0;
  for (size_t i = 0; i < callerPath.size(); ++i) {
    if (i != 0) {
      callerPathAsString.append("\n");
    }

    callerPathAsString.append(std::string(2 * indentation++, ' '));
    callerPathAsString.append(callerPath[i]);
  }

  return callerPathAsString;
}

#pragma mark - Database Schema

static const char *CreateTables = R"CreateTables(
CREATE TABLE execution_result (
  status INT,
  duration INT,
  stdout TEXT,
  stderr TEXT
);

CREATE TABLE test (
  test_name TEXT,
  execution_result_id INT
);

CREATE TABLE mutation_point (
  mutation_operator TEXT,
  module_name TEXT,
  function_name TEXT,
  function_index INT,
  basic_block_index INT,
  instruction_index INT,
  filename TEXT,
  line_number INT,
  column_number INT,
  __tmp_caller_path TEXT,
  unique_id TEXT UNIQUE
);

CREATE TABLE mutation_result (
  execution_result_id INT,
  test_id TEXT,
  mutation_point_id TEXT,
  mutation_distance INT
);

CREATE TABLE mutation_point_debug (
  filename TEXT,
  line_number INT,
  column_number INT,
  function TEXT,
  basic_block TEXT,
  instruction INT,
  unique_id TEXT UNIQUE);
)CreateTables";

static void createTables(sqlite3 *database) {
  sqlite_exec(database, CreateTables);
}
