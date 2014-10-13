#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <csignal>

#include "libporto.hpp"
#include "util/string.hpp"
#include "util/unix.hpp"
#include "util/namespace.hpp"
#include "cli.hpp"

extern "C" {
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <wordexp.h>
#include <termios.h>
#include <poll.h>
}

using std::string;
using std::vector;
using std::stringstream;
using std::ostream_iterator;
using std::map;
using std::pair;
using std::set;
using std::shared_ptr;

string HumanNsec(const string &val) {
    double n = stod(val);
    string suf = "ns";
    if (n > 1024) {
        n /= 1024;
        suf = "us";
    }
    if (n > 1024) {
        n /= 1024;
        suf = "ms";
    }
    if (n > 1024) {
        n /= 1024;
        suf = "s";
    }

    std::stringstream str;
    str << n << suf;
    return str.str();
}

string HumanSec(const string &val) {
    int64_t n = stoll(val);
    int64_t h = 0, m = 0, s = n;

    if (s > 60) {
        m = s / 60;
        s %= 60;
    }

    if (m > 60) {
        h = m / 60;
        m %= 60;
    }

    std::stringstream str;
    if (h)
        str << std::setfill('0') << std::setw(2) << h << ":";
    str << std::setfill('0') << std::setw(2) << m << ":";
    str << std::setfill('0') << std::setw(2) << s;
    return str.str();
}

string HumanSize(const string &val) {
    double n = stod(val);
    string suf = "";
    if (n > 1024) {
        n /= 1024;
        suf = "K";
    }
    if (n > 1024) {
        n /= 1024;
        suf = "M";
    }
    if (n > 1024) {
        n /= 1024;
        suf = "G";
    }

    std::stringstream str;
    str << n << suf;
    return str.str();
}

string PropertyValue(const string &name, const string &val) {
    if (name == "memory_guarantee" ||
        name == "memory_limit" ||
        name == "net_ceil" ||
        name == "net_guarantee") {
        return HumanSize(val);
    } else {
        return val;
    }
}

string DataValue(const string &name, const string &val) {
    if (val == "")
        return val;

    if (name == "exit_status") {
        int status;
        if (StringToInt(val, status))
            return val;

        string ret;

        if (WIFEXITED(status))
            ret = "Container exited with " + std::to_string(WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            ret = "Container killed by signal " + std::to_string(WTERMSIG(status));
        else if (status == 0)
            ret = "Success";

        return ret;
    } else if (name == "errno") {
        int status;
        if (StringToInt(val, status))
            return val;

        string ret;

        if (status < 0)
            ret = "Prepare failed: " + string(strerror(-status));
        else if (status > 0)
            ret = "Exec failed: " + string(strerror(status));
        else if (status == 0)
            ret = "Success";

        return ret + " (" + val + ")";
    } else if (name == "memory_usage" ||
               name == "net_drops" ||
               name == "net_overlimits" ||
               name == "net_packets" ||
               name == "net_bytes") {
        return HumanSize(val);
    } else if (name == "cpu_usage") {
        return HumanNsec(val);
    } else if (name == "time") {
        return HumanSec(val);
    } else {
        return val;
    }
}

size_t CalculateFieldLength(vector<string> &vec, size_t min = 8) {
    size_t len = 0;
    for (auto &i : vec)
        if (i.length() > len)
            len  = i.length();

    return (len > min ? len : min) + 1;
}

bool ValidData(const vector<TData> &dlist, const string &name) {
    return find_if(dlist.begin(), dlist.end(),
                   [&](const TData &i)->bool { return i.Name == name; })
        != dlist.end();
}

class TRawCmd : public ICmd {
public:
    TRawCmd(TPortoAPI *api) : ICmd(api, "raw", 2, "<message>", "send raw protobuf message") {}

    int Execute(int argc, char *argv[]) {
        stringstream msg;

        std::vector<std::string> args(argv, argv + argc);
        copy(args.begin(), args.end(), ostream_iterator<string>(msg, " "));

        string resp;
        if (!Api->Raw(msg.str(), resp))
            std::cout << resp << std::endl;

        return 0;
    }
};

class TCreateCmd : public ICmd {
public:
    TCreateCmd(TPortoAPI *api) : ICmd(api, "create", 1, "<name>", "create container") {}

    int Execute(int argc, char *argv[]) {
        int ret = Api->Create(argv[0]);
        if (ret)
            PrintError("Can't create container");

        return ret;
    }
};

class TGetPropertyCmd : public ICmd {
public:
    TGetPropertyCmd(TPortoAPI *api) : ICmd(api, "pget", 2, "<name> <property>", "get container property") {}

    int Execute(int argc, char *argv[]) {
        string value;
        int ret = Api->GetProperty(argv[0], argv[1], value);
        if (ret)
            PrintError("Can't get property");
        else
            std::cout << value << std::endl;

        return ret;
    }
};

class TSetPropertyCmd : public ICmd {
public:
    TSetPropertyCmd(TPortoAPI *api) : ICmd(api, "set", 3, "<name> <property>", "set container property") {}

    int Execute(int argc, char *argv[]) {
        string val = argv[2];
        for (int i = 3; i < argc; i++) {
            val += " ";
            val += argv[i];
        }

        int ret = Api->SetProperty(argv[0], argv[1], val);
        if (ret)
            PrintError("Can't set property");

        return ret;
    }
};

class TGetDataCmd : public ICmd {
public:
    TGetDataCmd(TPortoAPI *api) : ICmd(api, "dget", 2, "<name> <data>", "get container data") {}

    int Execute(int argc, char *argv[]) {
        string value;
        int ret = Api->GetData(argv[0], argv[1], value);
        if (ret)
            PrintError("Can't get data");
        else
            std::cout << value << std::endl;

        return ret;
    }
};

class TStartCmd : public ICmd {
public:
    TStartCmd(TPortoAPI *api) : ICmd(api, "start", 1, "<name>", "start container") {}

    int Execute(int argc, char *argv[]) {
        int ret = Api->Start(argv[0]);
        if (ret)
            PrintError("Can't start container");

        return ret;
    }
};

static const map<string, int> sigMap = {
    { "SIGHUP",     SIGHUP },
    { "SIGINT",     SIGINT },
    { "SIGQUIT",    SIGQUIT },
    { "SIGILL",     SIGILL },
    { "SIGABRT",    SIGABRT },
    { "SIGFPE",     SIGFPE },
    { "SIGKILL",    SIGKILL },
    { "SIGSEGV",    SIGSEGV },
    { "SIGPIPE",    SIGPIPE },

    { "SIGALRM",    SIGALRM },
    { "SIGTERM",    SIGTERM },
    { "SIGUSR1",    SIGUSR1 },
    { "SIGUSR2",    SIGUSR2 },
    { "SIGCHLD",    SIGCHLD },
    { "SIGCONT",    SIGCONT },
    { "SIGSTOP",    SIGSTOP },
    { "SIGTSTP",    SIGTSTP },
    { "SIGTTIN",    SIGTTIN },
    { "SIGTTOU",    SIGTTOU },

    { "SIGBUS",     SIGBUS },
    { "SIGPOLL",    SIGPOLL },
    { "SIGPROF",    SIGPROF },
    { "SIGSYS",     SIGSYS },
    { "SIGTRAP",    SIGTRAP },
    { "SIGURG",     SIGURG },
    { "SIGVTALRM",  SIGVTALRM },
    { "SIGXCPU",    SIGXCPU },
    { "SIGXFSZ",    SIGXFSZ },

    { "SIGIOT",     SIGIOT },
#ifdef SIGEMT
    { "SIGEMT",     SIGEMT },
#endif
    { "SIGSTKFLT",  SIGSTKFLT },
    { "SIGIO",      SIGIO },
    { "SIGCLD",     SIGCLD },
    { "SIGPWR",     SIGPWR },
#ifdef SIGINFO
    { "SIGINFO",    SIGINFO },
#endif
#ifdef SIGLOST
    { "SIGLOST",    SIGLOST },
#endif
    { "SIGWINCH",   SIGWINCH },
    { "SIGUNUSED",  SIGUNUSED },
};

class TKillCmd : public ICmd {
public:
    TKillCmd(TPortoAPI *api) : ICmd(api, "kill", 1, "<name> [signal]", "send signal to container") {}

    int Execute(int argc, char *argv[]) {
        int sig = SIGTERM;
        if (argc >= 2) {
            string sigName = argv[1];

            if (sigMap.find(sigName) != sigMap.end()) {
                sig = sigMap.at(sigName);
            } else {
                TError error = StringToInt(sigName, sig);
                if (error) {
                    PrintError(error, "Invalid signal");
                    return EXIT_FAILURE;
                }
            }
        }

        int ret = Api->Kill(argv[0], sig);
        if (ret)
            PrintError("Can't send signal to container");

        return ret;
    }
};

class TStopCmd : public ICmd {
public:
    TStopCmd(TPortoAPI *api) : ICmd(api, "stop", 1, "<name>", "stop container") {}

    int Execute(int argc, char *argv[]) {
        int ret = Api->Stop(argv[0]);
        if (ret)
            PrintError("Can't stop container");

        return ret;
    }
};

class TPauseCmd : public ICmd {
public:
    TPauseCmd(TPortoAPI *api) : ICmd(api, "pause", 1, "<name>", "pause container") {}

    int Execute(int argc, char *argv[]) {
        int ret = Api->Pause(argv[0]);
        if (ret)
            PrintError("Can't pause container");

        return ret;
    }
};

class TResumeCmd : public ICmd {
public:
    TResumeCmd(TPortoAPI *api) : ICmd(api, "resume", 1, "<name>", "resume container") {}

    int Execute(int argc, char *argv[]) {
        int ret = Api->Resume(argv[0]);
        if (ret)
            PrintError("Can't resume container");

        return ret;
    }
};

class TGetCmd : public ICmd {
public:
    TGetCmd(TPortoAPI *api) : ICmd(api, "get", 1, "<name> [data]", "get container property or data") {}

    bool ValidProperty(const vector<TProperty> &plist, const string &name) {
        return find_if(plist.begin(), plist.end(),
                       [&](const TProperty &i)->bool { return i.Name == name; })
            != plist.end();
    }

    int Execute(int argc, char *argv[]) {
        string value;
        int ret;

        vector<TProperty> plist;
        ret = Api->Plist(plist);
        if (ret) {
            PrintError("Can't list properties");
            return EXIT_FAILURE;
        }

        vector<TData> dlist;
        ret = Api->Dlist(dlist);
        if (ret) {
            PrintError("Can't list data");
            return EXIT_FAILURE;
        }

        if (argc <= 1) {
            int printed = 0;

            for (auto p : plist) {
                if (!ValidProperty(plist, p.Name))
                    continue;

                ret = Api->GetProperty(argv[0], p.Name, value);
                if (!ret) {
                    std::cout << p.Name << " = " << PropertyValue(p.Name, value) << std::endl;
                    printed++;
                }
            }

            for (auto d : dlist) {
                if (!ValidData(dlist, d.Name))
                    continue;

                ret = Api->GetData(argv[0], d.Name, value);
                if (!ret) {
                    std::cout << d.Name << " = " << DataValue(d.Name, value) << std::endl;
                    printed++;
                }
            }

            if (!printed)
                    std::cerr << "Invalid container name" << std::endl;

            return 0;
        }

        bool validProperty = ValidProperty(plist, argv[1]);
        bool validData = ValidData(dlist, argv[1]);

        if (validData) {
            ret = Api->GetData(argv[0], argv[1], value);
            if (!ret)
                std::cout << DataValue(argv[1], value) << std::endl;
            else if (ret != EError::InvalidData)
                PrintError("Can't get data");
        }

        if (validProperty) {
            ret = Api->GetProperty(argv[0], argv[1], value);
            if (!ret) {
                std::cout << PropertyValue(argv[1], value) << std::endl;
            } else if (ret != EError::InvalidProperty) {
                PrintError("Can't get data");
                return EXIT_FAILURE;
            }
        }

        if (!validProperty && !validData) {
            std::cerr << "Invalid property or data" << std::endl;
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }
};

class TEnterCmd : public ICmd {
public:
    TEnterCmd(TPortoAPI *api) : ICmd(api, "enter", 1, "<name> [-C] [command]", "execute command in container namespace") {}

    void PrintErrno(const string &str) {
        std::cerr << str << ": " << strerror(errno) << std::endl;
    }

    int OpenFd(int pid, string v) {
        string path = "/proc/" + std::to_string(pid) + "/" + v;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            PrintErrno("Can't open [" + path + "] " + std::to_string(fd));
            throw "";
        }

        return fd;
    }

    TError GetCgMount(const string &subsys, string &root) {
        vector<string> subsystems;
        TError error = SplitString(subsys, ',', subsystems);
        if (error)
            return error;

        TMountSnapshot ms;
        set<shared_ptr<TMount>> mounts;
        error = ms.Mounts(mounts);
        if (error)
            return error;

        for (auto &mount : mounts) {
            set<string> flags = mount->GetFlags();
            bool found = true;
            for (auto &ss : subsystems)
                if (flags.find(ss) == flags.end()) {
                    found = false;
                    break;
                }

            if (found) {
                root = mount->GetMountpoint();
                return TError::Success();
            }
        }

        return TError(EError::Unknown, "Can't find root for " + subsys);
    }

    int Execute(int argc, char *argv[]) {
        string cmd = "";
        int start = 1;
        bool enterCgroups = true;

        if (argc >= 2) {
            if (argv[1] == string("-C"))
                enterCgroups = false;

            start++;
        }

        for (int i = start; i < argc; i++) {
            cmd += argv[i];
            cmd += " ";
        }

        if (!cmd.length())
            cmd = "/bin/bash";

        string pidStr;
        int ret = Api->GetData(argv[0], "root_pid", pidStr);
        if (ret) {
            PrintError("Can't get container root_pid");
            return EXIT_FAILURE;
        }

        int pid;
        TError error = StringToInt(pidStr, pid);
        if (error) {
            PrintError(error, "Can't parse root_pid");
            return EXIT_FAILURE;
        }

        int rootFd = OpenFd(pid, "root");
        int cwdFd = OpenFd(pid, "cwd");

        if (enterCgroups) {
            map<string, string> cgmap;
            TError error = GetTaskCgroups(pid, cgmap);
            if (error) {
                PrintError(error, "Can't get task cgroups");
                return EXIT_FAILURE;

            }

            for (auto &cg : cgmap) {
                string root;
                TError error = GetCgMount(cg.first, root);
                if (error) {
                    PrintError(error, "Can't get task cgroups");
                    return EXIT_FAILURE;
                }

                TFile f(root + cg.second + "/cgroup.procs");
                error = f.AppendString(std::to_string(GetPid()));
                if (error) {
                    PrintError(error, "Can't get task cgroups");
                    return EXIT_FAILURE;
                }
            }
        }

        TNamespaceSnapshot ns;
        error = ns.Create(pid);
        if (error) {
            PrintError(error, "Can't create namespace snapshot");
            return EXIT_FAILURE;
        }

        error = ns.Attach();
        if (error) {
            PrintError(error, "Can't create namespace snapshot");
            return EXIT_FAILURE;
        }

        if (fchdir(rootFd) < 0) {
            PrintErrno("Can't change root directory");
            return EXIT_FAILURE;
        }

        if (chroot(".") < 0) {
            PrintErrno("Can't change root directory");
            return EXIT_FAILURE;
        }
        close(rootFd);

        if (fchdir(cwdFd) < 0) {
            PrintErrno("Can't change root directory");
            return EXIT_FAILURE;
        }
        close(cwdFd);

        wordexp_t result;
        ret = wordexp(cmd.c_str(), &result, WRDE_NOCMD | WRDE_UNDEF);
        if (ret) {
            errno = EINVAL;
            PrintErrno("Can't parse command");
            return EXIT_FAILURE;
        }

        int status = EXIT_FAILURE;
        int child = fork();
        if (child) {
            if (waitpid(child, &status, 0) < 0)
                PrintErrno("Can't wait child");
        } else if (child < 0) {
            PrintErrno("Can't fork");
        } else {
            execvp(result.we_wordv[0], (char *const *)result.we_wordv);
            PrintErrno("Can't execute " + string(result.we_wordv[0]));
        }

        return status;
    }
};

class TRunCmd : public ICmd {
public:
    TRunCmd(TPortoAPI *api) : ICmd(api, "run", 2, "<container> [properties]", "create and start container with given properties") {}

    int Parser(string property, map<string, string> &properties) {
        string propertyKey, propertyValue;
        string::size_type n;
        n = property.find('=');
        if (n == string::npos) {
            TError error(EError::InvalidValue, "Invalid value");
            PrintError(error, "Can't parse property: " + property);
            return EXIT_FAILURE;
        }
        propertyKey = property.substr(0, n);
        propertyValue = property.substr(n + 1, property.size());
        if (propertyKey == "" || propertyValue == "") {
            TError error(EError::InvalidValue, "Invalid value");
            PrintError(error, "Can't parse property: " + property);
            return EXIT_FAILURE;
        }
        properties[propertyKey] = propertyValue;
        return EXIT_SUCCESS;
    }

    int Execute(int argc, char *argv[]) {
        string containerName = argv[0];
        map<string, string> properties;
        int ret;

        for (int i = 1; i < argc; i++) {
            ret = Parser(argv[i], properties);
            if (ret)
                return ret;
        }

        ret = Api->Create(containerName);
        if (ret) {
            PrintError("Can't create container");
            return EXIT_FAILURE;
        }
        for (auto iter: properties) {
            ret = Api->SetProperty(containerName, iter.first, iter.second);
            if (ret) {
                PrintError("Can't set property");
                (void)Api->Destroy(containerName);
                return EXIT_FAILURE;
            }
        }
        ret = Api->Start(containerName);
        if (ret) {
            PrintError("Can't start property");
            (void)Api->Destroy(containerName);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
};

static struct termios savedAttrs;
static string destroyContainerName;
static void resetInputMode(void) {
  tcsetattr (STDIN_FILENO, TCSANOW, &savedAttrs);
}
static void destroyContainer(void) {
    if (destroyContainerName != "") {
        TPortoAPI api(config().rpc_sock().file().path());
        (void)api.Destroy(destroyContainerName);
    }
}

class TExecCmd : public ICmd {
    string containerName;
public:
    TExecCmd(TPortoAPI *api) : ICmd(api, "exec", 2, "<container> [properties]", "execute and wait for command in container") {}

    int SwithToNonCanonical(int fd) {
        if (!isatty(fd))
            return 0;

        if (tcgetattr(fd, &savedAttrs) < 0)
            return -1;
        atexit(resetInputMode);

        static struct termios t = savedAttrs;
        t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
        t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR |
                       INPCK | ISTRIP | IXON | PARMRK);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        return tcsetattr(fd, TCSAFLUSH, &t);
    }

    void MoveData(int from, int to) {
        char buf[256];
        int ret;

        ret = read(from, buf, sizeof(buf));
        if (ret > 0)
            if (write(to, buf, ret) != ret)
                std::cerr << "Partial write to " << to << std::endl;
    }

    int Execute(int argc, char *argv[]) {
        containerName = argv[0];
        bool needEnv = isatty(STDIN_FILENO);
        bool haveEnv = false;
        string env;

        vector<const char *> args;
        for (int i = 0; i < argc; i++) {
            if (needEnv && strncmp(argv[i], "env=", 4) == 0) {
                env = string(argv[i]) + ";TERM=" + getenv("TERM");
                args.push_back(env.c_str());
                haveEnv = true;
            } else {
                args.push_back(argv[i]);
            }
        }

        int ptm = posix_openpt(O_RDWR);
        if (ptm < 0) {
            TError error(EError::Unknown, errno, "posix_openpt()");
            PrintError(error, "Can't open pseudoterminal");
            return EXIT_FAILURE;
        }

        if (grantpt(ptm) < 0) {
            TError error(EError::Unknown, errno, "grantpt()");
            PrintError(error, "Can't open pseudoterminal");
            return EXIT_FAILURE;
        }

        if (unlockpt(ptm) < 0) {
            TError error(EError::Unknown, errno, "unlockpt()");
            PrintError(error, "Can't open pseudoterminal");
            return EXIT_FAILURE;
        }

        char *slavept = ptsname(ptm);

        if (SwithToNonCanonical(STDIN_FILENO) < 0) {
            TError error(EError::Unknown, errno, "SwithToNonCanonical()");
            PrintError(error, "Can't open pseudoterminal");
            return EXIT_FAILURE;
        }

        string stdinPath = string("stdin_path=") + slavept;
        string stdoutPath = string("stdout_path=") + slavept;
        string stderrPath = string("stderr_path=") + slavept;

        args.push_back(stdinPath.c_str());
        args.push_back(stdoutPath.c_str());
        args.push_back(stderrPath.c_str());

        if (needEnv && !haveEnv) {
            env = string("env=TERM=") + getenv("TERM");
            args.push_back(env.c_str());
        }

        for (int i = 0; i < argc; i++)
            std::cerr << args[i] << std::endl;

        auto *run = new TRunCmd(Api);
        int ret = run->Execute(args.size(), (char **)args.data());
        if (ret)
            return ret;

        destroyContainerName = containerName;
        atexit(destroyContainer);

        vector<struct pollfd> fds;

        bool hangup = false;
        while (!hangup) {
            struct pollfd pfd = {};
            fds.clear();

            pfd.fd = STDIN_FILENO;
            pfd.events = POLLIN | POLLHUP;
            fds.push_back(pfd);

            pfd.fd = ptm;
            pfd.events = POLLIN | POLLHUP;
            fds.push_back(pfd);

            ret = poll(fds.data(), fds.size(), -1);
            if (ret < 0)
                break;

            for (size_t i = 0; i < fds.size(); i++) {
                if (fds[i].revents & POLLHUP)
                    hangup = true;

                if (!(fds[i].revents & POLLIN))
                    continue;

                if (fds[i].fd == STDIN_FILENO)
                    MoveData(STDIN_FILENO, ptm);
                else if (fds[i].fd == ptm)
                    MoveData(ptm, STDOUT_FILENO);
            }
        }

        string state;
        int loop = 1000;
        do {
            ret = Api->GetData(containerName, "state", state);
            if (ret) {
                PrintError("Can't get state");
                return EXIT_FAILURE;
            }
        } while (loop-- && state == "running");

        string s;
        ret = Api->GetData(containerName, "exit_status", s);
        if (ret) {
            PrintError("Can't get exit_status");
            return EXIT_FAILURE;
        }

        int status = stoi(s);
        if (WIFEXITED(status)) {
            exit(WEXITSTATUS(status));
        } else {
            ResetAllSignalHandlers();
            raise(WTERMSIG(status));
            exit(EXIT_FAILURE);
        }

        return EXIT_SUCCESS;
    }
};

class TDestroyCmd : public ICmd {
public:
    TDestroyCmd(TPortoAPI *api) : ICmd(api, "destroy", 1, "<name>", "destroy container") {}

    int Execute(int argc, char *argv[]) {
        int ret = Api->Destroy(argv[0]);
        if (ret)
            PrintError("Can't destroy container");

        return ret;
    }
};

class TListCmd : public ICmd {
public:
    TListCmd(TPortoAPI *api) : ICmd(api, "list", 0, "", "list created containers") {}

    int Execute(int argc, char *argv[]) {
        vector<string> clist;
        int ret = Api->List(clist);
        if (ret) {
            PrintError("Can't list containers");
            return ret;
        }

        vector<string> states = { "running", "dead", "stopped", "paused" };
        size_t stateLen = CalculateFieldLength(states);
        size_t nameLen = CalculateFieldLength(clist);
        size_t timeLen = 10;
        for (auto c : clist) {
            string s;
            ret = Api->GetData(c, "state", s);
            if (ret)
                PrintError("Can't get container state");

            std::cout << std::left << std::setw(nameLen) << c
                      << std::right << std::setw(stateLen) << s;

            if (s == "running") {
                string tm;
                ret = Api->GetData(c, "time", tm);
                if (!ret)
                        std::cout << std::right << std::setw(timeLen)
                            << DataValue("time", tm);
            }

            std::cout << std::endl;
        }

        return EXIT_SUCCESS;
    }
};

class TTopCmd : public ICmd {
public:
    TTopCmd(TPortoAPI *api) : ICmd(api, "top", 0, "[sort-by]", "print containers sorted by resource usage") {}

    int Execute(int argc, char *argv[]) {
        vector<string> clist;
        int ret = Api->List(clist);
        if (ret) {
            PrintError("Can't list containers");
            return EXIT_FAILURE;
        }

        vector<pair<string, map<string, string>>> containerData;
        vector<string> showData;

        if (argc == 0) {
            showData.push_back("cpu_usage");
            showData.push_back("memory_usage");
            showData.push_back("major_faults");
            showData.push_back("minor_faults");

            if (config().network().enabled())
                showData.push_back("net_packets");
        } else {
            vector<TData> dlist;
            ret = Api->Dlist(dlist);
            if (ret) {
                PrintError("Can't list data");
                return EXIT_FAILURE;
            }

            for (int i = 0; i < argc; i++) {
                string arg = argv[i];

                if (!ValidData(dlist, arg)) {
                    TError error(EError::InvalidValue, "Invalid value");
                    PrintError(error, "Can't parse argument");
                    return EXIT_FAILURE;
                }

                showData.push_back(arg);
            }
        }

        string sortBy = showData[0];
        size_t nameLen = CalculateFieldLength(clist, strlen("container"));

        for (auto container : clist) {
            string state;
            ret = Api->GetData(container, "state", state);
            if (ret) {
                PrintError("Can't get container state");
                return EXIT_FAILURE;
            }

            if (state != "running")
                continue;

            map<string, string> dataVal;
            for (auto data : showData) {
                string val;
                (void)Api->GetData(container, data, val);
                dataVal[data] = val;
            }

            containerData.push_back(make_pair(container, dataVal));
        }

        std::sort(containerData.begin(), containerData.end(),
                  [&](pair<string, map<string, string>> a,
                     pair<string, map<string, string>> b) {
                  string as, bs;
                  int64_t an, bn;

                  if (a.second.find(sortBy) != a.second.end())
                      as = a.second[sortBy];

                  if (b.second.find(sortBy) != b.second.end())
                      bs = b.second[sortBy];

                  TError aError = StringToInt64(as, an);
                  TError bError = StringToInt64(bs, bn);
                  if (aError || bError)
                      return as > bs;

                  return an > bn;
                  });

        vector<size_t> fieldLen;
        for (auto &data : showData) {
            vector<string> tmp;
            tmp.push_back(data);

            for (auto &pair : containerData)
                tmp.push_back(DataValue(data, pair.second[data]));

            fieldLen.push_back(CalculateFieldLength(tmp));
        }

        std::cout << std::left << std::setw(nameLen) << "container";
        for (size_t i = 0; i < showData.size(); i++)
            std::cout << std::right << std::setw(fieldLen[i]) << showData[i];
        std::cout << std::endl;

        for (auto &pair : containerData) {
            std::cout << std::left << std::setw(nameLen) << pair.first;

            for (size_t i = 0; i < showData.size(); i++) {
                std::cout << std::right << std::setw(fieldLen[i]);
                std::cout << DataValue(showData[i], pair.second[showData[i]]);
            }
            std::cout << std::endl;
        }

        return ret;
    }
};

int main(int argc, char *argv[]) {
    config.Load(true);
    TPortoAPI api(config().rpc_sock().file().path());

    RegisterCommand(new THelpCmd(&api, true));
    RegisterCommand(new TCreateCmd(&api));
    RegisterCommand(new TDestroyCmd(&api));
    RegisterCommand(new TListCmd(&api));
    RegisterCommand(new TTopCmd(&api));
    RegisterCommand(new TStartCmd(&api));
    RegisterCommand(new TStopCmd(&api));
    RegisterCommand(new TKillCmd(&api));
    RegisterCommand(new TPauseCmd(&api));
    RegisterCommand(new TResumeCmd(&api));
    RegisterCommand(new TGetPropertyCmd(&api));
    RegisterCommand(new TSetPropertyCmd(&api));
    RegisterCommand(new TGetDataCmd(&api));
    RegisterCommand(new TGetCmd(&api));
    RegisterCommand(new TRawCmd(&api));
    RegisterCommand(new TEnterCmd(&api));
    RegisterCommand(new TRunCmd(&api));
    RegisterCommand(new TExecCmd(&api));

    return HandleCommand(argc, argv);
};
