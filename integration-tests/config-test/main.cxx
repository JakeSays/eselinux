// Copyright (c) Jake Helfert
// Licensed under the MIT License.
//
// ese-config-test — integration test for the libucl-backed registry
// replacement (`dev/ese/src/os/posix/config_posix.cxx`).
//
// The Linux port reads engine configuration from two layered files:
//
//   * /etc/ese.conf            — system-wide defaults (always loaded)
//   * <per-binary or override>.ese.conf
//       Default lookup: readlink("/proc/self/exe") + ".ese.conf"
//       Override:       JetPlatformInitializeWithConfig(path) supplies
//                       the path explicitly
//
// The loader is one-shot per process: the merged UCL object is built
// the first time JetPlatformInitialize* runs and stays read-only for
// the rest of the process's life.  This test therefore drives each
// subtest in a fresh fork+execv child, with the conf-file path baked
// into argv so the child reads exactly the file we want for that
// subtest.

#include <jetapi.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace
{

std::string SelfExePath()
{
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
    {
        fprintf(stderr, "readlink(/proc/self/exe): %s\n", strerror(errno));
        exit(2);
    }
    buf[n] = '\0';
    return std::string(buf);
}

//  Per-subtest scratch path under /tmp, suffixed with the pid so two
//  concurrent runs don't trip over each other.
std::string ConfPathFor(std::string_view scenarioName)
{
    char path[256];
    snprintf(path, sizeof(path),
             "/tmp/ese-config-test.%d.%.*s.ese.conf",
             (int) getpid(),
             (int) scenarioName.size(),
             scenarioName.data());
    return path;
}

void WriteConf(const std::string& path, std::string_view contents)
{
    FILE* const f = fopen(path.c_str(), "w");
    if (!f)
    {
        fprintf(stderr, "fopen(%s): %s\n", path.c_str(), strerror(errno));
        exit(3);
    }
    if (fwrite(contents.data(), 1, contents.size(), f) != contents.size())
    {
        fprintf(stderr, "fwrite: %s\n", strerror(errno));
        fclose(f);
        exit(3);
    }
    fclose(f);
}

//  ----- Verifier-side helpers (run after exec) -----------------------

int g_failures = 0;

void RequireImpl(bool cond, const char* expr, const char* file, int line)
{
    if (!cond)
    {
        ++g_failures;
        fprintf(stderr, "  FAIL %s:%d: %s\n", file, line, expr);
    }
}

#define REQUIRE(expr) RequireImpl((expr), #expr, __FILE__, __LINE__)

JET_API_PTR GetParam(JET_INSTANCE instance, uint32_t paramid)
{
    JET_API_PTR value = 0;
    const JET_ERR err = JetGetSystemParameterA(instance, JET_sesidNil,
                                               paramid, &value,
                                               nullptr, 0);
    if (err < JET_errSuccess)
    {
        fprintf(stderr, "  JetGetSystemParameterA(paramid=%u) -> err=%d\n",
                paramid, err);
        ++g_failures;
    }
    return value;
}

//  Drive the engine to the point param overrides have been applied.
//  `confPath` is what JetPlatformInitializeWithConfig sees — an empty
//  string means "default behaviour (read /etc/ese.conf merged with
//  the per-binary conf)".
JET_INSTANCE BringUpEngine(const char* confPath)
{
    JET_ERR err = (confPath != nullptr && confPath[0] != '\0')
                  ? JetPlatformInitializeWithConfig(confPath)
                  : JetPlatformInitialize();
    if (err < JET_errSuccess)
    {
        fprintf(stderr, "  JetPlatformInitialize* -> err=%d\n", err);
        exit(4);
    }
    JET_INSTANCE inst = JET_instanceNil;
    err = JetCreateInstance2A(&inst,
                              "ese-config-test", "ese-config-test", 0);
    if (err < JET_errSuccess)
    {
        fprintf(stderr, "  JetCreateInstance2A -> err=%d\n", err);
        exit(4);
    }
    return inst;
}

void TearDownEngine(JET_INSTANCE inst)
{
    (void) JetTerm(inst);
    (void) JetPlatformTerminate();
}

//  ----- Subtests -------------------------------------------------------

int VerifyOverridesApplied(const char* confPath)
{
    JET_INSTANCE inst = BringUpEngine(confPath);
    REQUIRE(GetParam(inst, JET_paramMaxCursors)  == 137);
    REQUIRE(GetParam(inst, JET_paramMaxSessions) == 42);
    REQUIRE(GetParam(inst, JET_paramLogBuffers)  == 256);
    TearDownEngine(inst);
    return g_failures;
}

int VerifyHexAndStringForms(const char* confPath)
{
    JET_INSTANCE inst = BringUpEngine(confPath);
    REQUIRE(GetParam(inst, JET_paramMaxCursors)    == 0x99);
    REQUIRE(GetParam(inst, JET_paramMaxOpenTables) == 73);
    TearDownEngine(inst);
    return g_failures;
}

int VerifyNoConfDefaults(const char* /*confPath*/)
{
    //  Pass empty path so JetPlatformInitialize takes the default
    //  lookup arm; with no /etc/ese.conf and no <exe>.ese.conf, no
    //  overrides land and params keep their built-in defaults.
    JET_INSTANCE inst = BringUpEngine("");
    const JET_API_PTR maxCursors  = GetParam(inst, JET_paramMaxCursors);
    const JET_API_PTR maxSessions = GetParam(inst, JET_paramMaxSessions);
    REQUIRE(maxCursors  > 0);
    REQUIRE(maxSessions > 0);
    REQUIRE(maxCursors  != 137);  // sentinel from VerifyOverridesApplied
    REQUIRE(maxSessions != 42);
    TearDownEngine(inst);
    return g_failures;
}

int VerifyPartialOverride(const char* confPath)
{
    JET_INSTANCE inst = BringUpEngine(confPath);
    REQUIRE(GetParam(inst, JET_paramMaxCursors) == 211);
    const JET_API_PTR maxSessions = GetParam(inst, JET_paramMaxSessions);
    REQUIRE(maxSessions > 0);
    REQUIRE(maxSessions != 211);
    TearDownEngine(inst);
    return g_failures;
}

int VerifyMalformedConfDoesNotCrash(const char* confPath)
{
    JET_INSTANCE inst = BringUpEngine(confPath);
    //  Loader prints to stderr and proceeds with an empty merged
    //  object; param falls back to its built-in default.
    REQUIRE(GetParam(inst, JET_paramMaxCursors) > 0);
    TearDownEngine(inst);
    return g_failures;
}

int VerifyEmptyConfBlock(const char* confPath)
{
    JET_INSTANCE inst = BringUpEngine(confPath);
    const JET_API_PTR maxCursors = GetParam(inst, JET_paramMaxCursors);
    REQUIRE(maxCursors > 0);
    REQUIRE(maxCursors != 137);
    TearDownEngine(inst);
    return g_failures;
}

//  ----- Subtest table + fork+exec driver -----------------------------

struct Subtest
{
    const char* name;
    const char* confContents;   //  nullptr ⇒ don't plant a file
    int       (*verify)(const char* confPath);
};

const Subtest kSubtests[] = {
    {
        "OverridesApplied",
        "\"System Parameter Overrides\" {\n"
        "    MaxCursors  = 137\n"
        "    MaxSessions = 42\n"
        "    LogBuffers  = 256\n"
        "}\n",
        VerifyOverridesApplied,
    },
    {
        "HexAndStringForms",
        "\"System Parameter Overrides\" {\n"
        "    MaxCursors    = 0x99\n"
        "    MaxOpenTables = \"73\"\n"
        "}\n",
        VerifyHexAndStringForms,
    },
    {
        "NoConfDefaults",
        nullptr,
        VerifyNoConfDefaults,
    },
    {
        "PartialOverride",
        "\"System Parameter Overrides\" {\n"
        "    MaxCursors = 211\n"
        "}\n",
        VerifyPartialOverride,
    },
    {
        "MalformedConfDoesNotCrash",
        //  Unbalanced brace — parser rejects the file; loader prints
        //  to stderr and proceeds with an empty merged object.
        "\"System Parameter Overrides\" {\n"
        "    MaxCursors = 137\n"
        "  # intentional missing closing brace below\n",
        VerifyMalformedConfDoesNotCrash,
    },
    {
        "EmptyConfBlock",
        "\"System Parameter Overrides\" {\n"
        "}\n",
        VerifyEmptyConfBlock,
    },
};

const Subtest* FindSubtest(std::string_view name)
{
    for (const auto& subtest : kSubtests)
    {
        if (name == subtest.name)
        {
            return &subtest;
        }
    }
    return nullptr;
}

//  fork+execve a child in verifier mode.  argv carries the scenario
//  name and the conf-file path so the child can wire
//  JetPlatformInitializeWithConfig before any engine work happens.
int LaunchVerifier(const char* selfPath,
                   const char* scenarioName,
                   const char* confPath)
{
    fflush(nullptr);
    const pid_t pid = fork();
    if (pid < 0)
    {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return 99;
    }
    if (pid == 0)
    {
        char arg0[PATH_MAX];
        char arg1[]  = "--verify";
        char arg2[64];
        char arg3[PATH_MAX];
        snprintf(arg0, sizeof(arg0), "%s", selfPath);
        snprintf(arg2, sizeof(arg2), "%s", scenarioName);
        snprintf(arg3, sizeof(arg3), "%s", confPath);
        char* const childArgv[] = { arg0, arg1, arg2, arg3, nullptr };
        execv(selfPath, childArgv);
        fprintf(stderr, "execv(%s): %s\n", selfPath, strerror(errno));
        _exit(98);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
    {
        fprintf(stderr, "waitpid: %s\n", strerror(errno));
        return 99;
    }
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status))
    {
        fprintf(stderr, "  child died on signal %d\n", WTERMSIG(status));
        return 200 + WTERMSIG(status);
    }
    return 99;
}

int RunDriver(const char* selfPath)
{
    int passed = 0;
    int failed = 0;
    for (const auto& subtest : kSubtests)
    {
        fprintf(stderr, "[ RUN  ] %s\n", subtest.name);

        const std::string confPath = (subtest.confContents != nullptr)
                                     ? ConfPathFor(subtest.name)
                                     : std::string();
        if (subtest.confContents != nullptr)
        {
            WriteConf(confPath, subtest.confContents);
        }

        const int rc = LaunchVerifier(selfPath, subtest.name,
                                      confPath.c_str());

        if (!confPath.empty())
        {
            std::error_code ec;
            std::filesystem::remove(confPath, ec);
        }

        if (rc == 0)
        {
            fprintf(stderr, "[ PASS ] %s\n", subtest.name);
            ++passed;
        }
        else
        {
            fprintf(stderr, "[ FAIL ] %s (rc=%d)\n", subtest.name, rc);
            ++failed;
        }
    }

    fprintf(stderr, "\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0
           ? 0
           : 1;
}

int RunVerifier(const char* scenarioName, const char* confPath)
{
    const Subtest* const subtest = FindSubtest(scenarioName);
    if (subtest == nullptr)
    {
        fprintf(stderr, "  unknown scenario '%s'\n", scenarioName);
        return 97;
    }
    return subtest->verify(confPath);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 4 && strcmp(argv[1], "--verify") == 0)
    {
        return RunVerifier(argv[2], argv[3]);
    }
    return RunDriver(SelfExePath().c_str());
}
