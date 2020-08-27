/***********************************************************************
 *  wclang                                                             *
 *  Copyright (C) 2013-2019 Thomas Poechtrager                         *
 *  t.poechtrager@gmail.com                                            *
 *                                                                     *
 *  This program is free software; you can redistribute it and/or      *
 *  modify it under the terms of the GNU General Public License        *
 *  as published by the Free Software Foundation; either version 2     *
 *  of the License, or (at your option) any later version.             *
 *                                                                     *
 *  This program is distributed in the hope that it will be useful,    *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of     *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the      *
 *  GNU General Public License for more details.                       *
 *                                                                     *
 *  You should have received a copy of the GNU General Public License  *
 *  along with this program; if not, write to the Free Software        *
 *  Foundation, Inc.,                                                  *
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.      *
 ***********************************************************************/

#include <iostream>
#include <fstream>
#include <typeinfo>
#include <tuple>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include <climits>
#include <cstdlib>
#include <cassert>
#include "wclang.h"
#include "wclang_time.h"

/*
 * Supported targets
 */

enum {
    TARGET_WIN32 = 0,
    TARGET_WIN64
};

static constexpr const char* TARGET32[] = {
    "i686-w64-mingw32",
    "i686-w64-mingw32.static", /* MXE */
    "i686-w64-mingw32.shared", /* MXE */
    "i686-pc-mingw32",
    "i586-mingw32",
    "i586-mingw32msvc",
    "i486-mingw32"
};

static constexpr const char* TARGET64[] = {
    "x86_64-w64-mingw32",
    "x86_64-w64-mingw32.static", /* MXE */
    "x86_64-w64-mingw32.shared", /* MXE */
    "amd64-mingw32msvc"
};

/*
 * Additional C/C++ flags
 */
static constexpr char CXXFLAGS[] = "";
static constexpr char CFLAGS[] = "";

static constexpr const char* ENVVARS[] = {
    "AR", "AS", "CPP", "DLLTOOL", "DLLWRAP",
    "ELFEDIT","GCOV", "GNAT", "LD", "NM",
    "OBJCOPY", "OBJDUMP", "RANLIB", "READELF",
    "SIZE", "STRINGS", "STRIP", "WINDMC", "WINDRES"
};

static constexpr char COMMANDPREFIX[] = "-wc-";

#ifndef NO_SYS_PATH
/*
 * Paths where we should look for mingw C++ headers
 */

static constexpr const char* CXXINCLUDEBASE[] = {
    "/usr",
    "/usr/lib/gcc",
    "/usr/local/include/c++",
    "/usr/include/c++",
    "/opt"
};

/*
 * Paths where we should look for mingw C headers
 */

static constexpr const char* STDINCLUDEBASE[] = {
    "/usr",
    "/usr/local",
    "/opt"
};
#endif

static bool findcxxheaders(const char *target, commandargs &cmdargs)
{
    std::string root;
    std::string cxxheaders;
    std::string mingwheaders;
    auto &mv = cmdargs.mingwversion;
    auto &cxxpaths = cmdargs.cxxpaths;
    const auto &stdpaths = cmdargs.stdpaths;
    static const char *_target = target;

    auto checkmingwheaders = [](const char *dir, const char *file)
    {
        static struct stat st;
        static std::stringstream d;

        clear(d);
        d << dir << file << "/" << _target;

        return !stat(d.str().c_str(), &st);
    };

    auto checkheaderdir = [](const std::string &cxxheaderdir)
    {
        static std::string file;
        static struct stat st;

        file = cxxheaderdir;
        file += "/";
        file += "iostream";

        return !stat(file.c_str(), &st);
    };

    auto addheaderdir = [&](const std::string &cxxheaderdir, bool mingw)
    {
        static std::string mingwheaderdir;
        cxxpaths.push_back(cxxheaderdir);

        if (mingw)
        {
            mingwheaderdir = cxxheaderdir;
            mingwheaderdir += "/";
            mingwheaderdir += target;

            cxxpaths.push_back(mingwheaderdir);
        }
    };

    auto findheaders = [&]()
    {
        for (const auto &stddir : stdpaths)
        {
            /*
             * a: stddir / c++
             * b: a / xxxx-w64-mingw32
             */

            cxxheaders = stddir;
            cxxheaders += "/c++";
            cxxheaders += "/";

            if (checkheaderdir(cxxheaders))
            {
                addheaderdir(cxxheaders, true);
                return true;
            }

            /*
             * a: stddir / c++ / <gccver>
             * b: a / xxxx-w64-mingw32
             */

            mv = findlatestcompilerversion(cxxheaders.c_str());

            if (!mv.num())
                continue;

            cxxheaders += mv.s;

            if (checkheaderdir(cxxheaders))
            {
                addheaderdir(cxxheaders, true);
                return true;
            }
        }

#ifndef NO_SYS_PATH
        for (const char *cxxinclude : CXXINCLUDEBASE)
        {
            /*
             * a: root / cxxinclude / <gccver>
             * b: a / xxxx-w64-mingw32
             */

            cxxheaders = root;
            cxxheaders += cxxinclude;
            cxxheaders += "/";

            mv = findlatestcompilerversion(cxxheaders.c_str(),
                                           checkmingwheaders);

            if (!mv.num())
                continue;

            cxxheaders += mv.s;

            if (checkheaderdir(cxxheaders))
            {
                addheaderdir(cxxheaders, true);
                return true;
            }
        }

        for (const char *cxxinclude : CXXINCLUDEBASE)
        {
            /*
             * a: root / cxxinclude / <target> / <gccver> / include / c++
             * b: root / cxxinclude / <target> / <gccver> / xxxx-w64-mingw32
             */

            cxxheaders  = root;
            cxxheaders += cxxinclude;
            cxxheaders += "/";
            cxxheaders += target;
            cxxheaders += "/";

            mv = findlatestcompilerversion(cxxheaders.c_str(),
                                           checkmingwheaders);

            if (!mv.num())
                continue;

            mingwheaders = cxxheaders;

            cxxheaders += mv.s;
            cxxheaders += "/include/c++";

            if (checkheaderdir(cxxheaders))
            {
                addheaderdir(cxxheaders, false);
                addheaderdir(mingwheaders, false);
                return true;
            }
        }

        for (const char *cxxinclude : CXXINCLUDEBASE)
        {
            /*
             * a: root / cxxinclude / <target> / <gccver> / include / c++
             * b: a / xxxx-w64-mingw32
             */

            cxxheaders = root;
            cxxheaders += cxxinclude;
            cxxheaders += "/";
            cxxheaders += target;
            cxxheaders += "/";

            mv = findlatestcompilerversion(cxxheaders.c_str());

            if (!mv.num())
                continue;

            cxxheaders += mv.s;
            cxxheaders += "/include/c++";

            if (!checkmingwheaders(cxxheaders.c_str(), ""))
                continue;

            if (checkheaderdir(cxxheaders))
            {
                addheaderdir(cxxheaders, true);
                return true;
            }
        }
#endif

        return false;
    };

    root = stdpaths[0] + "/../../..";
    if (findheaders()) return true;

    root.clear();
    return findheaders();
}

static bool findintrinheaders(commandargs &cmdargs, const std::string &clangbindir)
{
    static std::stringstream dir;
    static compilerver *clangversion;
    static std::string pathtmp;

    clangversion = &cmdargs.clangversion;
    string_vector &intrinpaths = cmdargs.intrinpaths;

    clear(dir);
    *clangversion = compilerver();
    pathtmp.clear();

    auto trydir = [&]() -> bool
    {
        listfiles(dir.str().c_str(), nullptr, [](const char *, const char *file)
        {
            if (file[0] != '.' && isdirectory(file, dir.str().c_str()))
            {
                compilerver cv = parsecompilerversion(file);

                if (cv != compilerver())
                {
                    static std::stringstream tmp;
                    clear(tmp);

                    auto checkdir = [&](std::stringstream &dir)
                    {
                        static std::string intrindir;
                        auto &file = dir;

                        intrindir = dir.str();
                        file << "/xmmintrin.h";

                        if (fileexists(file.str().c_str()))
                        {
                            if (cv > *clangversion)
                            {
                                *clangversion = cv;
                                pathtmp.swap(intrindir);
                            }
                            return true;
                        }

                        return false;
                    };

                    tmp << dir.str() << "/" << file << "/include";

                    if (!checkdir(tmp))
                    {
                        clear(tmp);
                        tmp << dir.str() << "/" << file;
                        checkdir(tmp);
                    }
                }
                return true;
            }
            return true;
        });
        return *clangversion != compilerver();
    };

#define TRYDIR(basedir, subdir)               \
do                                            \
{                                             \
    dir << basedir << subdir;                 \
    if (trydir())                             \
    {                                         \
        intrinpaths.push_back(pathtmp);       \
        return true;                          \
    }                                         \
    clear(dir);                               \
} while (0)

#define TRYDIR2(libdir) TRYDIR(clangbindir, libdir)
#define TRYDIR3(libdir) TRYDIR(std::string(), libdir)

#ifdef __CYGWIN__
#ifdef __x86_64__
    TRYDIR2("/../lib/clang/x86_64-pc-cygwin");
#else
    TRYDIR2("/../lib/clang/i686-pc-cygwin");
#endif
#endif

    TRYDIR2("/../lib/clang");

#ifdef __linux__
#ifdef __x86_64__
    // opensuse uses lib64 instead of lib on x86_64
    TRYDIR2("/../lib64/clang");
#elif __i386__
    TRYDIR2("/../lib32/clang");
#endif
#endif

#ifdef __APPLE__
    constexpr const char *OSXIntrinDirs[] =
    {
        "/Library/Developer/CommandLineTools/usr/lib/clang",
        "/Applications/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang"
    };

    for (auto intrindir : OSXIntrinDirs) TRYDIR3(intrindir);
#endif

    TRYDIR2("/../include/clang");
    TRYDIR2("/usr/include/clang");

    return false;
#undef TRYDIR
#undef TRYDIR2
#undef TRYDIR3
}

static bool findstdheader(const char *target, commandargs &cmdargs)
{
    std::string dir;
    struct stat st;
    auto &stdpaths = cmdargs.stdpaths;
    const char *mingwpath = getenv("MINGW_PATH");

    auto checkdir = [&](const char *stdinclude) -> bool
    {
        auto trydir = [&](const std::string &dir) -> bool
        {
            if (!stat(dir.c_str(), &st) && S_ISDIR(st.st_mode))
            {
                std::string filecheck = dir + "/stdlib.h";

                if (stat(filecheck.c_str(), &st))
                    return false;

                stdpaths.push_back(dir);

#ifdef _DEBUG
                for (const auto &dir : stdpaths)
                    std::cout << "found C include dir: " << dir << std::endl;
#endif

                return true;
            }

            return false;
        };

        dir = stdinclude;
        dir += "/";
        dir += target;
        dir += "/include";

        if (trydir(dir))
            return true;

        dir = stdinclude;
        dir += "/";
        dir += target;
        dir += "/sys-root/mingw/include";

        if (trydir(dir))
            return true;

        // MXE
        dir = stdinclude;
        dir += "/usr/";
        dir += target;
        dir += "/include";

        if (trydir(dir))
            return true;

        return false;
    };

    auto checkpath = [&](const char *p) -> bool
    {
        std::string path;

        do
        {
            if (*p == ':') p++;

            while (*p && *p != ':')
                path += *p++;

            if (path.find_last_of("/bin") != std::string::npos)
                path.resize(path.size()-STRLEN("/bin"));

            if (checkdir(path.c_str()))
                return true;

            path.clear();
        } while (*p);

        return false;
    };

    if (mingwpath && *mingwpath)
        return checkpath(mingwpath);

#ifdef MINGW_PATH
    if (*MINGW_PATH && checkpath(MINGW_PATH))
        return true;
#endif

#ifndef NO_SYS_PATH
    for (const char *stdinclude : STDINCLUDEBASE)
        if (checkdir(stdinclude)) return true;
#endif

    return false;
}

static const char *findtarget32(commandargs &cmdargs)
{
    for (const char *target : TARGET32)
        if (findstdheader(target, cmdargs)) return target;

    return nullptr;
}

static const char *findtarget64(commandargs &cmdargs)
{
    for (const char *target : TARGET64)
        if (findstdheader(target, cmdargs)) return target;

    return nullptr;
}

static bool findheaders(commandargs &cmdargs, const std::string &target)
{
    return findstdheader(target.c_str(), cmdargs);
}

static const char *findtriple(const char *name, int &targettype)
{
    const char *p = std::strstr(name, "-clang");
    size_t len = p-name;

    if (!p)
        return nullptr;

    for (const char *target : TARGET32)
    {
        if (!std::strncmp(target, name, len) && !target[len])
        {
            targettype = TARGET_WIN32;
            return target;
        }
    }

    for (const char *target : TARGET64)
    {
        if (!std::strncmp(target, name, len) && !target[len])
        {
            targettype = TARGET_WIN64;
            return target;
        }
    }

    return nullptr;
}

void appendexetooutputname(char **cargs)
{
    const char *filename;
    const char *suffix;

    for (char **arg = cargs; *arg; ++arg)
    {
        if (!std::strncmp(*arg, "-o", STRLEN("-o")))
        {
            if (!(*arg)[2] && (!*++arg || **arg == '-'))
                break;

            if (!std::strncmp(*arg, "-o", STRLEN("-o")))
                filename = &(*arg)[2];
            else
                filename = *arg;

            suffix = std::strrchr(filename, '.');

            if (suffix)
            {
                if (!std::strcmp(suffix, ".exe") || !std::strcmp(suffix, ".dll") ||
                    !std::strcmp(suffix, ".S"))
                {
                    return;
                }
            }

            for (char **p = arg+1; *p; ++p)
            {
                if (!std::strcmp(*p, "-c"))
                    return;
            }

            std::cerr << R"(wclang: appending ".exe" to output filename ")"
                      << filename << R"(")" << std::endl;

            filename = nullptr;
            suffix = nullptr;

            const size_t len = std::strlen(*arg)+STRLEN(".exe")+1;
            *arg = static_cast<char*>(std::realloc(*arg, len));

            if (!*arg)
            {
                std::cerr << "out of memory" << std::endl;
                std::exit(EXIT_FAILURE);
            }

            std::strcat(*arg, ".exe");
            break;
        }
        else if (!std::strcmp(*arg, "-c")) {
            break;
        }
    }
}

/*
 * Tools
 */

bool isterminal()
{
    static bool first = false;
    static bool val;

    if (!first)
    {
        val = !!isatty(fileno(stderr));
        first = true;
    }

    return val;
}

void concatenvvariable(const char *var, const std::string val, std::string *nval)
{
    std::string tmp;
    if (!nval) nval = &tmp;
    *nval = val;

    if (char *oldval = getenv(var))
    {
        *nval += ":";
        *nval += oldval;
    }

    setenv(var, nval->c_str(), 1);
}

compilerver parsecompilerversion(const char *compilerversion)
{
    const char *p = compilerversion;
    compilerver ver;

    std::strncpy(ver.s, compilerversion, sizeof(ver.s)-1);
    ver.major = atoi(p);

    while (*p && *p++ != '.')
        ;

    if (!*p)
        return ver;

    ver.minor = atoi(p);

    if (!*p)
        return ver;

    while (*p && *p++ != '.')
        ;

    if (!*p)
        return ver;

    ver.patch = atoi(p);
    return ver;
}

compilerver findlatestcompilerversion(const char *dir, listfilescallback cmp)
{
    static std::vector<compilerver> v;
    static std::vector<std::string> dirs;

    dirs.clear();

    if (!listfiles(dir, &dirs, cmp))
        return compilerver();

    if (dirs.empty())
        return compilerver();

    v.clear();

    for (auto &d : dirs)
        v.push_back(parsecompilerversion(d.c_str()));

    std::sort(v.begin(), v.end());
    return v[v.size() - 1];
}

bool fileexists(const char *file)
{
    struct stat st;
    return !stat(file, &st);
}

bool isdirectory(const char *file, const char *prefix)
{
    struct stat st;

    if (prefix)
    {
        std::string tmp = prefix;
        tmp += "/";
        tmp += file;
        return !stat(tmp.c_str(), &st) && S_ISDIR(st.st_mode);
    }

    return !stat(file, &st) && S_ISDIR(st.st_mode);
}

bool listfiles(const char *dir, std::vector<std::string> *files,
               listfilescallback cmp)
{
    DIR *d = opendir(dir);
    dirent *de;

    if (!d)
        return false;

    if (files)
        files->clear();

    while ((de = readdir(d)))
    {
        if (de->d_name[0] == '.' || !std::strcmp(de->d_name, ".."))
            continue;

        if ((!cmp || cmp(dir, de->d_name)) && files)
            files->push_back(de->d_name);
    }

    closedir(d);
    return true;
}

const char *getfileName(const char *file)
{
    const char *p = strrchr(file, PATHDIV);
    if (!p) p = file;
    else ++p;
    return p;
}


bool ignoreccache(const char *f, const struct stat &)
{
    const char *name = getfileName(f);
    return name && strstr(name, "ccache") != name;
}

bool wcrealpath(const char *file, std::string &result,
                realpathcmp cmp1, realpathcmp cmp2,
                const size_t maxSymbolicLinkDepth)
{
    char *PATH = getenv("PATH");
    const char *p = PATH ? PATH : "";
    struct stat st;

    result.clear();

    do
    {
        if (*p == ':') ++p;
        while (*p && *p != ':') result += *p++;

        result += "/";
        result += file;

        if (!stat(result.c_str(), &st))
        {
            if (maxSymbolicLinkDepth == 0)
                return true;

            char buf[PATH_MAX + 1];

            if (realpath(result.c_str(), buf))
            {
                result.assign(buf);
            }
            else
            {
                ssize_t len;
                char path[PATH_MAX];
                size_t pathlen;
                size_t n = 0;

                pathlen = result.find_last_of(PATHDIV);

                if (pathlen == std::string::npos) pathlen = result.length();
                else ++pathlen; // PATHDIV
        
                memcpy(path, result.c_str(), pathlen); // not null terminated

                while ((len = readlink(result.c_str(), buf, PATH_MAX)) != -1)
                {
                    if (buf[0] != PATHDIV)
                    {
                        result.assign(path, pathlen);
                        result.append(buf, len);
                    }
                    else
                    {
                        result.assign(buf, len);
                        pathlen = strrchr(buf, PATHDIV) - buf + 1; // + 1: PATHDIV
                        memcpy(path, buf, pathlen);
                    }

                    if (++n >= maxSymbolicLinkDepth)
                    {
                        result.clear();
                        break;
                    }
                }
            }

            if ((!cmp1 || cmp1(result.c_str(), st)) &&
                (!cmp2 || cmp2(result.c_str(), st)))
            break;
        }

        result.clear();
    } while (*p);

    return !result.empty();
}

bool getpathofcommand(const char *command, std::string &result)
{
    wcrealpath(command, result, [](const char *f, const struct stat&){
        return !access(f, F_OK|X_OK);
    }, ignoreccache);

    size_t pos = result.find_last_of("/");

    if (pos != std::string::npos)
        result.resize(pos);

    return !result.empty();
}

int runcommand(const char *command, char *buf, size_t len)
{
    if (!len)
        return RUNCOMMAND_ERROR;

    FILE *p;
    size_t outputlen;

    if (!(p = popen(command, "r")) || !(outputlen = fread(buf, sizeof(char), len - 1, p)))
    {
        if (p) pclose(p);
        return RUNCOMMAND_ERROR;
    }

    buf[outputlen] = '\0';
    return pclose(p);
}

void stripfilename(char *path)
{
    char *p = strrchr(path, '/');
    if (*p) *p = '\0';
}

template<class... T>
static void envvar(string_vector &env, const char *varname,
                   const char *val, T... values)
{
    const char *vals[] = { val, values... };

    std::string var = varname + std::string("=");

    /* g++ 4.7.2 doesn't like auto in this loop */
    for (const char *val : vals) var += val;

    env.push_back(var);
}

static void fmtstring(std::ostringstream &sbuf, const char *s)
{
    while (*s)
    {
        if (*s == '%')
        {
            if (s[1] == '%') ++s;
            else ERROR("fmtstring() error");
        }

        sbuf << *s++;
    }
}

template<typename T, typename... Args>
static std::string fmtstring(std::ostringstream &buf, const char *str,
                             T value, Args... args)
{
    while (*str)
    {
        if (*str == '%')
        {
            if (str[1] != '%')
            {
                buf << value;
                fmtstring(buf, str + 1, args...);
                return buf.str();
            }
            else {
                ++str;
            }
        }

        buf << *str++;
    }

    ERROR("fmtstring() error");
}

template<typename T = const char*, typename... Args>
static void verbosemsg(const char *str, T value, Args... args)
{
    std::ostringstream buf;
    std::string msg = fmtstring(buf, str, value, std::forward<Args>(args)...);
    std::cerr << PACKAGE_NAME << ": verbose: " << msg << std::endl;
}

template<typename T = const char*, typename... Args>
static void warn(const char *str, T value, Args... args)
{
    std::ostringstream buf;
    std::string warnmsg = fmtstring(buf, str, value, std::forward<Args>(args)...);
    if (isterminal())
    {
        std::cerr << KBLD PACKAGE_NAME ": warning: " KNRM << warnmsg << std::endl;
        return;
    }
    std::cerr << "warning: " << warnmsg << std::endl;
}

static void warn(const char *str)
{
    warn("%", str);
}

static time_vector times;
static time_point start = getticks();

static void timepoint(const char *description)
{
    time_point now = getticks();
    times.push_back(time_tuple(description, now));
}

static void printtimes()
{
    for (const auto &tp : times)
    {
        float ms = getmicrodiff(start, std::get<1>(tp)) / 1000.0f;
        verbosemsg("% +% ms", std::get<0>(tp), ms);
    }
}

static void parseargs(int argc, char **argv, const char *target,
                      commandargs &cmdargs, const string_vector &env)
{
    typedef void (*dcfun)(commandargs &cmdargs, char *arg);
    typedef std::tuple<dcfun, char*> dc_tuple;
    std::vector<dc_tuple> delayedcommands;

    auto printheader = []()
    {
        std::cout << PACKAGE_NAME << ", Version: " << PACKAGE_VERSION << std::endl;
    };

    for (int i = 0; i < argc; ++i)
    {
        char *arg = argv[i];

        if (*arg != '-')
            continue;

        switch (*(arg+1))
        {
            case 'c':
            {
                if (!std::strcmp(arg, "-c") || !std::strcmp(arg, "-S"))
                {
                    cmdargs.iscompilestep = true;
                    continue;
                }
                break;
            }
            case 'f':
            {
                if (cmdargs.iscxx)
                {
                    if (!std::strcmp(arg, "-fexceptions"))
                    {
                        cmdargs.exceptions = 1;
                        continue;
                    }
                    else if (!std::strcmp(arg, "-fno-exceptions"))
                    {
                        cmdargs.exceptions = 0;
                        continue;
                    }
                }
                break;
            }
            case 'm':
            {
                if (!std::strcmp(arg, "-mwindows") && cmdargs.usemingwlinker == subsystem::standard)
                {
                    /*
                     * Clang doesn't support -mwindows (yet)
                     */

                    cmdargs.usemingwlinker = subsystem::windows;
                    continue;
                }
                else if (!std::strcmp(arg, "-mdll") && cmdargs.usemingwlinker == subsystem::standard)
                {
                    /*
                     * Clang doesn't support -mdll (yet)
                     */

                    cmdargs.usemingwlinker = subsystem::dll;
                    continue;
                }
                else if (!std::strcmp(arg, "-mconsole") && cmdargs.usemingwlinker == subsystem::standard)
                {
                    /*
                     * Clang doesn't support -mconsole (yet)
                     */

                    cmdargs.usemingwlinker = subsystem::console;
                    continue;
                }
                break;
            }
            case 'o':
            {
                if (!std::strncmp(arg, "-o", STRLEN("-o")))
                {
                    cmdargs.islinkstep = true;
                    continue;
                }
                break;
            }
            case 'x':
            {
                if (!std::strncmp(arg, "-x", STRLEN("-x")))
                {
                    const char *p = arg+STRLEN("-x");

                    if (!*p)
                    {
                        p = argv[++i];

                        if (i >= argc)
                            ERROR("missing argument for '-x'");
                    }

                    auto checkcxx = [&]()
                    {
                        if (!cmdargs.iscxx)
                        {
                            cmdargs.iscxx = true;
                            findcxxheaders(target, cmdargs);
                        }
                    };

                    if (!std::strcmp(p, "c")) cmdargs.iscxx = false;
                    else if (!std::strcmp(p, "c-header")) cmdargs.iscxx = false;
                    else if (!std::strcmp(p, "c++")) checkcxx();
                    else if (!std::strcmp(p, "c++-header")) checkcxx();
                    else ERROR("given language not supported");
                    continue;
                }
                break;
            }
            case 'O':
            {
                if (!std::strncmp(arg, "-O", STRLEN("-O")))
                {
                    int &level = cmdargs.optimizationlevel;

                    arg += STRLEN("-O");

                    if (*arg == 's') level = optimize::SIZE_1;
                    else if (*arg == 'z') level = optimize::SIZE_2;
                    else if (!strcmp(arg, "fast")) level = optimize::FAST;
                    else {
                        level = std::atoi(arg);
                        if (level > optimize::LEVEL_3) level = optimize::LEVEL_3;
                        else if (level < optimize::LEVEL_0) level = optimize::LEVEL_0;
                    }
                    continue;
                }
                break;
            }
            break;
        }

        /*
         * Everything with COMMANDPREFIX belongs to us
         */

        if (!std::strncmp(arg, "--", STRLEN("--")))
            ++arg;

        if (std::strncmp(arg, COMMANDPREFIX, STRLEN(COMMANDPREFIX)))
            continue;

        arg += STRLEN(COMMANDPREFIX);

        #define INVALID_ARGUMENT else goto invalid_argument

        switch (*arg)
        {
            case 'a':
            {
                if (!std::strcmp(arg, "arch") || !std::strcmp(arg, "a"))
                {
                    const char *end = std::strchr(target, '-');

                    if (!end)
                    {
                        std::cerr << "internal error (could not determine arch)"
                                  << std::endl;
                        std::exit(EXIT_FAILURE);
                    }

                    std::string arch(target, end-target);
                    std::cout << arch << std::endl;
                    std::exit(EXIT_SUCCESS);
                }
                else if (!std::strcmp(arg, "append-exe")) {
                    cmdargs.appendexe = true;
                } INVALID_ARGUMENT;
                break;
            }
            case 'e':
            {
                if (!std::strncmp(arg, "env-", STRLEN("env-")) ||
                    !std::strncmp(arg, "e-", STRLEN("e-")))
                {
                    bool found = false;

                    while (*++arg != '-');
                    ++arg;

                    for (char *p = arg; *p; ++p) *p = toupper(*p);

                    size_t i = 0;
                    for (const char *var : ENVVARS)
                    {
                        if (!std::strcmp(arg, var))
                        {
                            const char *val = env[i].c_str();
                            val += std::strlen(var) + 1; /* skip variable name */

                            std::cout << val << std::endl;
                            found = true;

                            break;
                        }

                        if (found) break;

                        ++i;
                    }

                    if (!found)
                    {
                        std::cerr << "environment variable " << arg << " not found"
                                  << std::endl
                                  << "available environment variables: "
                                  << std::endl;

                        for (const char *var : ENVVARS)
                            std::cerr << " " << var << std::endl;

                        std::exit(EXIT_FAILURE);
                    }
                    std::exit(EXIT_SUCCESS);
                }
                else if (!std::strcmp(arg, "env") || !std::strcmp(arg, "e"))
                {
                    for (const auto &v : env) std::cout << v << " ";
                    std::cout << std::endl;
                    std::exit(EXIT_SUCCESS);
                } INVALID_ARGUMENT;
                break;
            }
            case 'h':
            {
                if (!std::strcmp(arg, "help") || !std::strcmp(arg, "h"))
                {
                    printheader();

                    auto printcmdhelp = [&](const char *cmd, const std::string &text)
                    {
                        std::cout << " " << COMMANDPREFIX << cmd << ": " << text << std::endl;
                    };

                    printcmdhelp("version", "show version");
                    printcmdhelp("target", "show target");

                    printcmdhelp("env-<var>", std::string("show environment variable  [e.g.: ") +
                                 std::string(COMMANDPREFIX) + std::string("env-ld]"));

                    printcmdhelp("env", "show all environment variables at once");
                    printcmdhelp("arch", "show target architecture");
                    printcmdhelp("static-runtime", "link runtime statically");
                    printcmdhelp("append-exe", "append .exe automatically to output filenames");
                    printcmdhelp("use-mingw-linker", "link with mingw");
                    printcmdhelp("no-intrin", "do not use clang intrinsics");
                    printcmdhelp("verbose", "enable verbose messages");

                    std::exit(EXIT_SUCCESS);
                } INVALID_ARGUMENT;
                break;
            }
            case 'n':
            {
                if (!std::strncmp(arg, "no-intrin", STRLEN("no-intrin")))
                {
                    cmdargs.nointrinsics = true;
                    continue;
                } INVALID_ARGUMENT;
                break;
            }
            case 's':
            {
                if (!std::strcmp(arg, "static-runtime"))
                {
                    static constexpr const char* GCCRUNTIME = "-static-libgcc";
                    static constexpr const char* LIBSTDCXXRUNTIME = "-static-libstdc++";

                    /*
                     * Postpone execution to later
                     * We don't know yet, if it is the link step or not
                     */
                    auto staticruntime = [](commandargs &cmdargs, char *arg)
                    {
                        /*
                         * Avoid "argument unused during compilation: '...'"
                         */
                        if (!cmdargs.islinkstep)
                        {
                            if (cmdargs.verbose)
                                verbosemsg("ignoring %", arg);
                            return;
                        }

                        if (cmdargs.iscxx)
                        {
                            cmdargs.cxxflags.push_back(GCCRUNTIME);
                            cmdargs.cxxflags.push_back(LIBSTDCXXRUNTIME);
                        }
                        else {
                            cmdargs.cflags.push_back(GCCRUNTIME);
                        }
                    };

                    delayedcommands.push_back(dc_tuple(staticruntime, arg-STRLEN(COMMANDPREFIX)));
                } INVALID_ARGUMENT;
                break;
            }
            case 't':
            {
                if (!std::strcmp(arg, "target") || !std::strcmp(arg, "t"))
                {
                    std::cout << target << std::endl;
                    std::exit(EXIT_SUCCESS);
                } INVALID_ARGUMENT;
                break;
            }
            case 'u':
            {
                if (!std::strcmp(arg, "use-mingw-linker"))
                {
                    auto usemingwlinker = [](commandargs &cmdargs, char *arg)
                    {
                        if (!cmdargs.islinkstep)
                        {
                            if (cmdargs.verbose)
                                verbosemsg("ignoring %", arg);
                            return;
                        }

                        cmdargs.usemingwlinker = subsystem::use_mingw_linker;
                    };

                    delayedcommands.push_back(dc_tuple(usemingwlinker, arg-STRLEN(COMMANDPREFIX)));
                    continue;
                } INVALID_ARGUMENT;
                break;
            }
            case 'v':
            {
                if (!std::strcmp(arg, "version") || !std::strcmp(arg, "v"))
                {
                    printheader();
                    std::cout << "Copyright (C) 2013-2017 Thomas Poechtrager" << std::endl;
                    std::cout << "License: GPL v2" << std::endl;
                    std::cout << "Bugs / Wishes: " << PACKAGE_BUGREPORT << std::endl;
                    std::exit(EXIT_SUCCESS);
                }
                else if (!std::strcmp(arg, "verbose")) {
                    cmdargs.verbose = true;
                } INVALID_ARGUMENT;
                break;
            }
            default:
            {
                invalid_argument:;
                printheader();
                std::cerr << "invalid argument: " << COMMANDPREFIX << arg << std::endl;
                std::exit(EXIT_FAILURE);
            }
        }

        #undef INVALID_ARGUMENT
    }

    if (cmdargs.islinkstep && cmdargs.iscompilestep)
    {
        /* w32-clang file.c -c -o file.o */
        cmdargs.islinkstep = false;
    }
    else if (!cmdargs.islinkstep && !cmdargs.iscompilestep)
    {
        /* w32-clang file.c */
        cmdargs.islinkstep = true;
    }

    for (auto dc : delayedcommands)
    {
        auto fun = std::get<0>(dc);
        fun(cmdargs, std::get<1>(dc));
    }
}

int main(int argc, char **argv)
{
    std::string target;
    int targettype = -1;
    const char *e = std::strrchr(argv[0], '/');
    const char *p = nullptr;

    bool iscxx = false;
    string_vector intrinpaths;
    string_vector stdpaths;
    string_vector cxxpaths;
    string_vector linkerflags;
    std::string compiler;
    std::string compilerpath;
    std::string compilerbinpath;
    string_vector env;
    string_vector args;
    char **cargs = nullptr;
    int cargsi = 0;
    string_vector cflags;
    string_vector cxxflags;

    commandargs cmdargs(intrinpaths, stdpaths, cxxpaths, cflags,
                        cxxflags, linkerflags, target,
                        compiler, compilerpath, compilerbinpath, env,
                        args, iscxx);

    timepoint("start");

    if (!e) e = argv[0];
    else ++e;

    p = std::strrchr(e, '-');
    if (!p++ || std::strncmp(p, "clang", STRLEN("clang")))
    {
        std::cerr << "invalid invocation name: clang should be followed "
                     "after target (e.g.: w32-clang)" << std::endl;
        return 1;
    }

    /*
     * Check if we want the C or the C++ compiler
     */

    p += STRLEN("clang");
    if (!std::strcmp(p, "++")) iscxx = true;
    else if (*p) {
        std::cerr << "invalid invocation name: ++ (or nothing) should be "
                     "followed after clang (e.g.: w32-clang++)" << std::endl;
        return 1;
    }

    /*
     * Check if we should target win32 or win64...
     */

    find_target_and_headers:;

    if (const char *triple = findtriple(e, targettype))
    {
        target = triple;
        findheaders(cmdargs, target);
    }
    else
    {
        if (!std::strncmp(e, "w32", STRLEN("w32")))
        {
            const char *t = findtarget32(cmdargs);
            target = t ? t : "";
            targettype = TARGET_WIN32;
        }
        else if (!std::strncmp(e, "w64", STRLEN("w64")))
        {
            const char *t = findtarget64(cmdargs);
            target = t ? t : "";
            targettype = TARGET_WIN64;
        }
    }

    if (targettype == -1)
    {
        std::cerr << "invalid target: " << e << std::endl;
        return 1;
    }
    else if (target.empty())
    {
        const char *type;
        std::string desc;

        if (getenv("MINGW_PATH"))
        {
            warn("MINGW_PATH env variable does not point to any "
                 "valid mingw installation for the current target!");

#ifdef HAVE_UNSETENV
            unsetenv("MINGW_PATH");
#else
            std::exit(EXIT_FAILURE);
#endif

            goto find_target_and_headers;
        }

        switch (targettype)
        {
            case TARGET_WIN32: type = "32 bit"; break;
            case TARGET_WIN64: type = "64 bit"; break;
            default: type = nullptr;
        }

        desc = std::string("mingw-w64 (") + std::string(type) + std::string(")");

        std::cerr << "cannot find " << desc << " installation" << std::endl;
        std::cerr << "make sure " << desc << " is installed on your system"
                  << std::endl;

        std::cerr << "if you have moved your mingw installation, "
                     "then re-run the installation process" << std::endl;
        return 1;
    }

    /*
     * Lookup C and C++ include paths
     */

    if (stdpaths.empty())
    {
        std::cerr << "cannot find " << target
                  << " C headers" << std::endl;

        std::cerr << "make sure " << target
                  << " C headers are installed on your system " << std::endl;
        return 1;
    }

    if (!findcxxheaders(target.c_str(), cmdargs) && iscxx)
    {
        std::cerr << "cannot find " << target
                  << " C++ headers" << std::endl;

        std::cerr << "make sure " << target
                  << " C++ headers are installed on your system "
                  << std::endl;
        return 1;
    }

    /*
     * Setup compiler command
     */

    if (iscxx)
    {
        compiler = "clang++";

        if (STRLEN(CXXFLAGS) > 0)
            cxxflags.push_back(CXXFLAGS);
    }
    else
    {
        compiler = "clang";

        if (STRLEN(CFLAGS) > 0)
            cflags.push_back(CFLAGS);
    }

    /*
     * Setup environment variables
     */

    for (const char *var : ENVVARS)
    {
        size_t len = std::strlen(var);
        char *buf = new char[1+len+1];

        buf[len+1] = '\0';
        *buf++ = '-';

        for (size_t i = 0; i < len; ++i)
            buf[i] = tolower(var[i]);

        envvar(env, var, target.c_str(), --buf);
        delete[] buf;
    }

    /*
     * Parse command arguments late,
     * when we know our environment already
     */

    parseargs(argc, argv, target.c_str(), cmdargs, env); /* may not return */

    /*
     * Setup compiler Arguments
     */

    if (cmdargs.islinkstep)
    {
        switch (cmdargs.usemingwlinker) {
            case subsystem::standard:
                break;
            case subsystem::use_mingw_linker:
                compiler = target + (iscxx ? "-g++" : "-gcc");
                break;
            case subsystem::console:
                linkerflags.push_back("-Wl,--subsystem,console");
                break;
            case subsystem::windows:
                linkerflags.push_back("-Wl,--subsystem,windows");
                break;
            case subsystem::dll:
                linkerflags.push_back("-Wl,--subsystem,dll");
                break;
        }
    }

    if ((targettype == TARGET_WIN64) &&
        (cmdargs.iscompilestep || cmdargs.islinkstep) &&
        (cmdargs.optimizationlevel >= optimize::LEVEL_1))
    {
        /*
         * Workaround for a bug in the MinGW math.h header,
         * fabs() and friends getting miscompiled without
         * defining __CRT__NO_INLINE, because there is
         * something wrong with their inline definition.
         */
        char *p;
        if (!(p = getenv("WCLANG_NO_CRT_INLINE_WORKAROUND")) || *p == '0')
        {
            // warn("defining __CRT__NO_INLINE to work around bugs in"
            //      "the mingw math.h header");
            cxxflags.push_back("-D__CRT__NO_INLINE");
        }
    }

    if (compiler[0] != '/')
    {
        std::string tmp;

        if (!getpathofcommand(compiler.c_str(), compilerbinpath))
        {
            std::cerr << "cannot find '" << compiler << "' executable"
                      << std::endl;
            return 1;
        }

        tmp.swap(compiler);

        compiler = compilerbinpath;
        compiler += "/";
        compiler += tmp;
    }

    {
        /*
         * Find MinGW binaries (required for linking)
         */
        std::string gcc = target + (iscxx ? "-g++" : "-gcc");
        std::string path;

        const char *mingwpath = getenv("MINGW_PATH");

        if (!mingwpath)
        {
#ifdef MINGW_PATH
            if (*MINGW_PATH) mingwpath = MINGW_PATH;
#endif
        }

        if (mingwpath && *mingwpath)
            concatenvvariable("PATH", mingwpath);

        if (!getpathofcommand(gcc.c_str(), path))
        {
            std::cerr << "cannot find " << gcc << " executable" << std::endl;
            return 1;
        }

#if 0
        /*
         * COMPILER_PATH would be a perfect solution to get rid of the
         * MinGW-GCC symlinks, but unfortunately it causes MinGW-GCC
         * to use the wrong assembler/linker on some systems.
         * GCC is invoked for assembling (prior to clang 3.5) and linking.
         */
        concatenvvariable("COMPILER_PATH", path, &cmdargs.compilerpath);
#endif

        if (cmdargs.islinkstep)
        {
            /* https://github.com/tpoechtrager/wclang/issues/22 */

            std::string command = cmdargs.target + "-gcc -print-libgcc-file-name";
            char output[4096];

            if (runcommand(command.c_str(), output, sizeof(output)) == 0)
            {
                stripfilename(output);
                linkerflags.push_back(std::string("-L") + output);
            }
        }


        args.push_back(compiler);

        auto pushcompilerflags = [&](const string_vector &flags)
        {
            for (const auto &flag : flags)
                args.push_back(flag);
        };

        pushcompilerflags(iscxx ? cxxflags : cflags);
        pushcompilerflags(linkerflags);

        if (!cmdargs.islinkstep || cmdargs.usemingwlinker != subsystem::standard)
        {
            char *p;

            args.push_back(CLANG_TARGET_OPT);
            args.push_back(target);

            /*
             * Prevent clang from including /usr/include in
             * case a file is not found in our directories
             */
            args.push_back("-nostdinc");
            args.push_back("-nostdinc++");
            args.push_back("-Qunused-arguments");

            auto pushdirs = [&](const string_vector &paths)
            {
                for (const auto &dir : paths)
                {
                    args.push_back("-isystem");
                    args.push_back(dir);
                }
            };

            if (!findintrinheaders(cmdargs, compilerbinpath))
            {
                if (!cmdargs.nointrinsics)
                    warn("cannot find clang intrinsics directory");
            }
            else if (cmdargs.clangversion == compilerver(3, 5, 0))
            {
                /*
                 * Workaround for clang 3.5.0 to get rid of
                 * error: redeclaration of '_scanf_l' cannot add 'dllimport' attribute
                 */
                args.push_back("-D_STDIO_S_DEFINED");
            }

            if (cmdargs.verbose)
                verbosemsg("detected clang version: %", cmdargs.clangversion.str());

            if (cmdargs.exceptions != 0 && (cmdargs.clangversion < compilerver(3, 7, 0) ||
                (targettype == TARGET_WIN32 && cmdargs.clangversion < compilerver(6, 0, 0))))
            {
                char *p;
                if (!(p = getenv("WCLANG_FORCE_CXX_EXCEPTIONS")) || *p == '0')
                {
                    if (cmdargs.exceptions == 1)
                    {
                        warn("-fexceptions will be replaced with -fno-exceptions: "
                             "exceptions are not supported (yet)");

                        std::cerr << "set WCLANG_FORCE_CXX_EXCEPTIONS to 1 "
                                  << "(env. variable) to force C++ exceptions" << std::endl;
                    }

                    args.push_back("-fno-exceptions");
                }
                else {
                    cmdargs.exceptions = -1;
                }
            }

            if (targettype == TARGET_WIN32 && cmdargs.clangversion >= compilerver(6, 0, 0))
            {
              args.push_back("-fsjlj-exceptions");
            }

            if ((p = getenv("WCLANG_NO_INTEGRATED_AS")) && *p == '1')
                args.push_back("-no-integrated-as");

            /*
             * For libstdc++ 6, the C++ includes must appear before the standard
             * includes.
             *
             * libstdc++ 6 is very picky if you use -isystem for system include
             * directories. It needs the C++ path first, otherwise it errors out
             * with "'stdlib.h' file not found".
             *
             * This is a known problem and apparently will not be fixed upstream:
             *
             * https://gcc.gnu.org/bugzilla/show_bug.cgi?id=70129
             */
            pushdirs(intrinpaths);
            pushdirs(cxxpaths);
            pushdirs(stdpaths);
        }
    }

    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        static constexpr size_t BUFSIZE = STRLEN(COMMANDPREFIX)+2;
        char buf[BUFSIZE];

        if (!std::strncmp(arg, COMMANDPREFIX, STRLEN(COMMANDPREFIX)))
            continue;

        if (cmdargs.exceptions == 1 && !std::strcmp(arg, "-fexceptions"))
            continue;

        *buf = '-';
        std::memcpy(buf+1, COMMANDPREFIX, BUFSIZE-1);

        if (!std::strncmp(arg, buf, BUFSIZE))
            continue;

        if (cmdargs.islinkstep && cmdargs.usemingwlinker != subsystem::standard)
        {
            if (!std::strncmp(arg, "-Qunused", STRLEN("-Qunused")))
                continue;
        }

        args.push_back(argv[i]);
    }

    cargs = new char* [args.size()+2];
    cargs[args.size()] = nullptr;

    for (const auto &opt : args)
        cargs[cargsi++] = strdup(opt.c_str());

    if (cmdargs.appendexe)
        appendexetooutputname(cargs);

    /*
     * Execute command
     */

    if (cmdargs.verbose)
    {
        std::string commandin, commandout;

        for (int i = 0; i < argc; ++i)
        {
            if (i) commandin += " ";
            commandin += argv[i];
        }

        for (char **arg = cargs; *arg; ++arg)
        {
            if (arg != cargs) commandout += " ";
            commandout += *arg;
        }

        timepoint("end");
        verbosemsg("command in: %", commandin);
        verbosemsg("command out: %", commandout);
        printtimes();
    }

    execvp(compiler.c_str(), cargs);

    std::cerr << "invoking compiler failed" << std::endl;
    std::cerr << compiler << " not installed?" << std::endl;
    return 1;
}
