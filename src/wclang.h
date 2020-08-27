#include <iostream>
#include <utility>
#include <cstdlib>
#include <string>
#include <sstream>
#include <vector>
#include "config.h"

static inline void ERRORMSG(const char *msg, const char *file,
                            int line, const char *func)
{
    std::cerr << "runtime error: " << msg << std::endl;
    std::cerr << file << " " << func << "():";
    std::cerr << line << std::endl;

    std::exit(EXIT_FAILURE);
}

__attribute__ ((noreturn))
static inline void ERRORMSG(const char *msg, const char *file,
                            int line, const char *func);

#define ERROR(msg) \
do { \
    ERRORMSG(msg, __FILE__, __LINE__, __func__); \
} while (0)

template<class T>
constexpr size_t STRLEN(const T &str)
{
    static_assert(sizeof(*str) == 1, "not a string");
    return sizeof(T)-1;
}

static_assert(STRLEN("test string") == 11, "");

static inline void clear(std::stringstream &s)
{
    s.str(std::string());
}

typedef unsigned long long ullong;
typedef std::vector<std::string> string_vector;

#define KNRM "\x1B[0m"
#define KBLD "\x1B[1m"
#define PATHDIV '/'

void concatenvvariable(const char *var, const std::string val, std::string *nval = nullptr);

typedef bool (*listfilescallback)(const char *dir, const char *file);
bool fileexists(const char *file);
bool isdirectory(const char *file, const char *prefix);
bool listfiles(const char *dir, std::vector<std::string> *files, listfilescallback cmp = nullptr);
const char *getfileName(const char *file);

typedef bool (*realpathcmp)(const char *file, const struct stat &st);
bool ignoreccache(const char *f, const struct stat &);
bool wcrealpath(const char *file, std::string &result, realpathcmp cmp1 = nullptr,
                realpathcmp cmp2 = nullptr, const size_t maxSymobolicLinkDepth = 1000);
bool getpathofcommand(const char *bin, std::string &result);

constexpr int RUNCOMMAND_ERROR = -100000;
int runcommand(const char *command, char *buf, size_t len);

void stripfilename(char *path);

struct compilerversion;
typedef compilerversion compilerver;
compilerver parsecompilerversion(const char *compilerversion);
compilerver findlatestcompilerversion(const char *dir, listfilescallback cmp = nullptr);

#undef major
#undef minor
#undef patch

struct compilerversion {
    constexpr compilerversion(int major, int minor, int patch = 0)
    : major(major), minor(minor), patch(patch), s() {}
    constexpr compilerversion() : major(), minor(), patch(), s() {}

    constexpr int num() const
    {
        return major * 10000 + minor * 100 + patch;
    }

    constexpr bool operator>(const compilerversion &cv) const
    {
        return num() > cv.num();
    }

    constexpr bool operator>=(const compilerversion &cv) const
    {
        return num() >= cv.num();
    }

    constexpr bool operator<(const compilerversion &cv) const
    {
        return num() < cv.num();
    }

    constexpr bool operator<=(const compilerversion &cv) const
    {
        return num() <= cv.num();
    }

    constexpr bool operator==(const compilerversion &cv) const
    {
        return num() == cv.num();
    }

    constexpr bool operator!=(const compilerversion &cv) const
    {
        return num() != cv.num();
    }

    bool operator!=(const char *val) const
    {
        size_t c = 0;
        const char *p = val;

        while (*p)
        {
            if (*p++ == '.')
                ++c;
        }

        switch (c)
        {
        case 1:
            return shortstr() != val;
        case 2:
            return str() != val;
        default:
            return true;
        }
    }

    std::string str() const
    {
        std::stringstream tmp;
        tmp << major << "." << minor << "." << patch;
        return tmp.str();
    }

    std::string shortstr() const
    {
        std::stringstream tmp;
        tmp << major << "." << minor;
        return tmp.str();
    }

    int major;
    int minor;
    int patch;
    char s[12];
};

enum optimize {
    LEVEL_0,
    LEVEL_1,
    LEVEL_3,
    FAST,
    SIZE_1,
    SIZE_2
};

enum class subsystem {
    standard = 0,
    use_mingw_linker,
    windows,
    dll,
    console
};

struct commandargs {
    bool verbose;
    compilerver clangversion;
    compilerver mingwversion;
    string_vector &intrinpaths;
    string_vector &stdpaths;
    string_vector &cxxpaths;
    string_vector &cflags;
    string_vector &cxxflags;
    string_vector &linkerflags;
    std::string &target;
    std::string &compiler;
    std::string &compilerpath;
    std::string &compilerbinpath;
    string_vector &env;
    string_vector &args;
    bool &iscxx;
    bool appendexe;
    bool iscompilestep;
    bool islinkstep;
    bool nointrinsics;
    int exceptions;
    int optimizationlevel;
    subsystem usemingwlinker;

    commandargs(string_vector &intrinpaths, string_vector &stdpaths, string_vector &cxxpaths,
                string_vector &cflags, string_vector &cxxflags,
                string_vector &linkerflags, std::string &target, std::string &compiler,
                std::string &compilerpath, std::string &compilerbinpath, string_vector &env,
                string_vector &args, bool &iscxx)
                :
                verbose(false), intrinpaths(intrinpaths), stdpaths(stdpaths),
                cxxpaths(cxxpaths), cflags(cflags), cxxflags(cxxflags),
                linkerflags(linkerflags), target(target), compiler(compiler), compilerpath(compilerpath),
                compilerbinpath(compilerbinpath), env(env), args(args), iscxx(iscxx),
                appendexe(false), iscompilestep(false), islinkstep(false), nointrinsics(false),
                exceptions(-1), optimizationlevel(0), usemingwlinker(subsystem::standard) {}
} __attribute__ ((aligned (8)));
