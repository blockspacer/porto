#include <iostream>
#include <iomanip>
#include <csignal>
#include <cstdio>
#include <algorithm>

#include "rpc.pb.h"
#include "libporto.hpp"
#include "util/file.hpp"
#include "util/string.hpp"
#include "util/unix.hpp"
#include "test.hpp"

extern "C" {
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
}

using std::string;
using std::vector;
using std::map;
using std::pair;

namespace test {

static void ExpectCorrectCgroups(const string &pid, const string &name) {
    auto cgmap = GetCgroups(pid);
    string subsystems[] = { "net_cls", "freezer", "memory", "cpu", "cpuacct" };
    int expected = sizeof(subsystems) / sizeof(subsystems[0]);

    for (auto kv : cgmap) {
        vector<string> cgsubsystems;
        ExpectSuccess(SplitString(kv.first, ',', cgsubsystems));

        for (auto &subsys : subsystems) {
            if (std::find(cgsubsystems.begin(), cgsubsystems.end(), subsys) != cgsubsystems.end()) {
                Expect(kv.second == "/porto/" + name);
                expected--;
            }
        }
    }
    Expect(expected == 0);
}

static void ShouldHaveOnlyRoot(TPortoAPI &api) {
    std::vector<std::string> containers;

    containers.clear();
    ExpectSuccess(api.List(containers));
    Expect(containers.size() == 1);
    Expect(containers[0] == string("/"));
}

static void ShouldHaveValidProperties(TPortoAPI &api, const string &name) {
    string v;

    ExpectSuccess(api.GetProperty(name, "command", v));
    Expect(v == string(""));
    ExpectSuccess(api.GetProperty(name, "user", v));
    Expect(v == GetDefaultUser());
    ExpectSuccess(api.GetProperty(name, "group", v));
    Expect(v == GetDefaultGroup());
    ExpectSuccess(api.GetProperty(name, "env", v));
    Expect(v == string(""));
    ExpectSuccess(api.GetProperty(name, "memory_guarantee", v));
    Expect(v == string("0"));
    ExpectSuccess(api.GetProperty(name, "memory_limit", v));
    Expect(v == string("0"));
    ExpectSuccess(api.GetProperty(name, "cpu_policy", v));
    Expect(v == string("normal"));
    ExpectSuccess(api.GetProperty(name, "cpu_priority", v));
    Expect(v == std::to_string(DEF_CLASS_PRIO));
    ExpectSuccess(api.GetProperty(name, "net_guarantee", v));
    Expect(v == std::to_string(DEF_CLASS_RATE));
    ExpectSuccess(api.GetProperty(name, "net_ceil", v));
    Expect(v == std::to_string(DEF_CLASS_CEIL));
    ExpectSuccess(api.GetProperty(name, "net_priority", v));
    Expect(v == std::to_string(DEF_CLASS_NET_PRIO));
    ExpectSuccess(api.GetProperty(name, "respawn", v));
    Expect(v == string("false"));
    ExpectSuccess(api.GetProperty(name, "cpu.smart", v));
    Expect(v == string("0"));
    ExpectSuccess(api.GetProperty(name, "memory.limit_in_bytes", v));
    Expect(v == string("0"));
    ExpectSuccess(api.GetProperty(name, "memory.low_limit_in_bytes", v));
    Expect(v == string("0"));
    ExpectSuccess(api.GetProperty(name, "memory.recharge_on_pgfault", v));
    Expect(v == string("0"));

}

static void ShouldHaveValidData(TPortoAPI &api, const string &name) {
    string v;

    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == string("stopped"));
    ExpectFailure(api.GetData(name, "exit_status", v), EError::InvalidState);
    ExpectSuccess(api.GetData(name, "start_errno", v));
    Expect(v == string("-1"));
    ExpectFailure(api.GetData(name, "root_pid", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "stdout", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "stderr", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "cpu_usage", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "memory_usage", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "net_bytes", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "net_packets", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "net_drops", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "net_overlimits", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "oom_killed", v), EError::InvalidState);
    ExpectFailure(api.GetData(name, "respawn_count", v), EError::InvalidState);
    ExpectSuccess(api.GetData(name, "parent", v));
    Expect(v == string("/"));
}

static void ExpectTclass(string name, bool exp) {
    string cls = GetCgKnob("net_cls", name, "net_cls.classid");
    Expect(TcClassExist(cls) == exp);
}

static void TestHolder(TPortoAPI &api) {
    ShouldHaveOnlyRoot(api);

    std::vector<std::string> containers;

    Say() << "Create container A" << std::endl;
    ExpectSuccess(api.Create("a"));
    containers.clear();
    ExpectSuccess(api.List(containers));
    Expect(containers.size() == 2);
    Expect(containers[0] == string("/"));
    Expect(containers[1] == string("a"));
    ShouldHaveValidProperties(api, "a");
    ShouldHaveValidData(api, "a");

    Say() << "Try to create existing container A" << std::endl;
    ExpectFailure(api.Create("a"), EError::ContainerAlreadyExists);
    containers.clear();
    ExpectSuccess(api.List(containers));
    Expect(containers.size() == 2);
    Expect(containers[0] == string("/"));
    Expect(containers[1] == string("a"));
    ShouldHaveValidProperties(api, "a");
    ShouldHaveValidData(api, "a");

    Say() << "Create container B" << std::endl;
    ExpectSuccess(api.Create("b"));
    containers.clear();
    ExpectSuccess(api.List(containers));
    Expect(containers.size() == 3);
    Expect(containers[0] == string("/"));
    Expect(containers[1] == string("a"));
    Expect(containers[2] == string("b"));
    ShouldHaveValidProperties(api, "b");
    ShouldHaveValidData(api, "b");

    Say() << "Remove container A" << std::endl;
    ExpectSuccess(api.Destroy("a"));
    containers.clear();
    ExpectSuccess(api.List(containers));
    Expect(containers.size() == 2);
    Expect(containers[0] == string("/"));
    Expect(containers[1] == string("b"));

    Say() << "Remove container B" << std::endl;
    ExpectSuccess(api.Destroy("b"));

    Say() << "Try to execute operations on invalid container" << std::endl;
    ExpectFailure(api.Start("a"), EError::ContainerDoesNotExist);
    ExpectFailure(api.Stop("a"), EError::ContainerDoesNotExist);
    ExpectFailure(api.Pause("a"), EError::ContainerDoesNotExist);
    ExpectFailure(api.Resume("a"), EError::ContainerDoesNotExist);

    string value;
    ExpectFailure(api.GetProperty("a", "command", value), EError::ContainerDoesNotExist);
    ExpectFailure(api.SetProperty("a", "command", value), EError::ContainerDoesNotExist);
    ExpectFailure(api.GetData("a", "root_pid", value), EError::ContainerDoesNotExist);

    Say() << "Try to create container with invalid name" << std::endl;
    string name;

    name = "z@";
    ExpectFailure(api.Create(name), EError::InvalidValue);

    name = "/invalid";
    ExpectFailure(api.Create(name), EError::InvalidValue);

    name = "invalid/";
    ExpectFailure(api.Create(name), EError::InvalidValue);

    name = "i//nvalid";
    ExpectFailure(api.Create(name), EError::InvalidValue);

    name = "invalid//";
    ExpectFailure(api.Create(name), EError::InvalidValue);

    name = "invali//d";
    ExpectFailure(api.Create(name), EError::InvalidValue);

    name = string(128, 'a');
    ExpectSuccess(api.Create(name));
    ExpectSuccess(api.Destroy(name));

    name = string(128, 'z');
    ExpectSuccess(api.Create(name));
    ExpectSuccess(api.Destroy(name));

    name = string(129, 'z');
    ExpectFailure(api.Create(name), EError::InvalidValue);

    string parent = "a";
    string child = "a/b";
    ExpectFailure(api.Create(child), EError::InvalidValue);
    ExpectSuccess(api.Create(parent));
    ExpectSuccess(api.Create(child));
    ExpectFailure(api.Destroy(parent), EError::InvalidState);
    ExpectSuccess(api.Destroy(child));
    ExpectSuccess(api.Destroy(parent));

    Say() << "Test hierarchy" << std::endl;
    ExpectSuccess(api.Create("a"));
    containers.clear();
    ExpectSuccess(api.List(containers));
    Expect(containers.size() == 2);
    Expect(containers[0] == string("/"));
    Expect(containers[1] == string("a"));

    ExpectSuccess(api.Create("a/b"));
    containers.clear();
    ExpectSuccess(api.List(containers));
    Expect(containers.size() == 3);
    Expect(containers[0] == string("/"));
    Expect(containers[1] == string("a"));
    Expect(containers[2] == string("a/b"));

    Say() << "Make sure child can stop only when parent is running" << std::endl;

    ExpectSuccess(api.Create("a/b/c"));
    containers.clear();
    ExpectSuccess(api.List(containers));
    Expect(containers.size() == 4);
    Expect(containers[0] == string("/"));
    Expect(containers[1] == string("a"));
    Expect(containers[2] == string("a/b"));
    Expect(containers[3] == string("a/b/c"));

    ExpectSuccess(api.SetProperty("a", "command", "sleep 1000"));
    ExpectSuccess(api.SetProperty("a/b", "command", "sleep 1000"));
    ExpectSuccess(api.SetProperty("a/b/c", "command", "sleep 1000"));
    ExpectFailure(api.Start("a/b/c"), EError::InvalidState);
    ExpectFailure(api.Start("a/b"), EError::InvalidState);
    ExpectSuccess(api.Start("a"));
    ExpectFailure(api.Start("a/b/c"), EError::InvalidState);
    ExpectSuccess(api.Start("a/b"));
    ExpectSuccess(api.Start("a/b/c"));

    Say() << "Make sure when parent stops/dies children are stopped" << std::endl;

    string state;

    ExpectSuccess(api.GetData("a/b/c", "state", state));
    Expect(state == "running");
    Expect(CgExists("memory", "a") == true);
    Expect(CgExists("memory", "a/b") == true);
    Expect(CgExists("memory", "a/b/c") == true);

    ExpectSuccess(api.Stop("a/b"));
    ExpectSuccess(api.GetData("a/b/c", "state", state));
    Expect(state == "stopped");
    ExpectSuccess(api.GetData("a/b", "state", state));
    Expect(state == "stopped");
    ExpectSuccess(api.GetData("a", "state", state));
    Expect(state == "running");
    Expect(CgExists("memory", "a") == true);
    Expect(CgExists("memory", "a/b") == false);
    Expect(CgExists("memory", "a/b/c") == false);

    ExpectSuccess(api.SetProperty("a/b", "command", "sleep 1"));
    ExpectSuccess(api.Start("a/b"));
    ExpectSuccess(api.Start("a/b/c"));
    Expect(CgExists("memory", "a") == true);
    Expect(CgExists("memory", "a/b") == true);
    Expect(CgExists("memory", "a/b/c") == true);

    ExpectTclass("a", true);
    ExpectTclass("a/b", true);
    ExpectTclass("a/b/c", true);

    WaitState(api, "a/b", "dead");
    ExpectSuccess(api.GetData("a/b", "state", state));
    Expect(state == "dead");
    ExpectSuccess(api.GetData("a/b/c", "state", state));
    Expect(state == "stopped");
    Expect(CgExists("memory", "a") == true);
    Expect(CgExists("memory", "a/b") == true);
    Expect(CgExists("memory", "a/b/c") == false);

    ExpectSuccess(api.Destroy("a/b/c"));
    ExpectSuccess(api.Destroy("a/b"));
    ExpectSuccess(api.Destroy("a"));

    ShouldHaveOnlyRoot(api);
}

static void TestEmpty(TPortoAPI &api) {
    Say() << "Make sure we can't start empty container" << std::endl;
    ExpectSuccess(api.Create("b"));
    ExpectFailure(api.Start("b"), EError::InvalidValue);
    ExpectSuccess(api.Destroy("b"));
}

static bool TaskRunning(TPortoAPI &api, const string &pid) {
    int p = stoi(pid);
    return kill(p, 0) == 0;
}

static bool TaskZombie(TPortoAPI &api, const string &pid) {
    return GetState(pid) == "Z";
}

static void TestExitStatus(TPortoAPI &api) {
    string pid;
    string ret;

    string name = "a";
    ExpectSuccess(api.Create(name));

    Say() << "Check exit status of 'false'" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "false"));
    ExpectSuccess(api.Start(name));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "exit_status", ret));
    Expect(ret == string("256"));
    ExpectSuccess(api.GetData(name, "oom_killed", ret));
    Expect(ret == string("false"));
    ExpectFailure(api.GetData(name, "start_errno", ret), EError::InvalidState);
    ExpectSuccess(api.Stop(name));

    Say() << "Check exit status of 'true'" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "true"));
    ExpectSuccess(api.Start(name));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "exit_status", ret));
    Expect(ret == string("0"));
    ExpectSuccess(api.GetData(name, "oom_killed", ret));
    Expect(ret == string("false"));
    ExpectFailure(api.GetData(name, "start_errno", ret), EError::InvalidState);
    ExpectSuccess(api.Stop(name));

    Say() << "Check exit status of invalid command" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "__invalid_command_name__"));
    ExpectSuccess(api.SetProperty(name, "cwd", "/"));
    ExpectFailure(api.Start(name), EError::Unknown);
    ExpectFailure(api.GetData(name, "root_pid", ret), EError::InvalidState);
    ExpectFailure(api.GetData(name, "exit_status", ret), EError::InvalidState);
    ExpectFailure(api.GetData(name, "oom_killed", ret), EError::InvalidState);
    ExpectSuccess(api.GetData(name, "start_errno", ret));
    Expect(ret == string("2"));

    Say() << "Check exit status of invalid directory" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "true"));
    ExpectSuccess(api.SetProperty(name, "cwd", "/__invalid__dir__"));
    ExpectFailure(api.Start(name), EError::Unknown);
    ExpectFailure(api.GetData(name, "root_pid", ret), EError::InvalidState);
    ExpectFailure(api.GetData(name, "exit_status", ret), EError::InvalidState);
    ExpectFailure(api.GetData(name, "oom_killed", ret), EError::InvalidState);
    ExpectSuccess(api.GetData(name, "start_errno", ret));
    Expect(ret == string("-2"));

    Say() << "Check exit status when killed by signal" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.SetProperty(name, "cwd", ""));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));
    kill(stoi(pid), SIGKILL);
    WaitState(api, name, "dead");
    Expect(TaskRunning(api, pid) == false);
    ExpectSuccess(api.GetData(name, "exit_status", ret));
    Expect(ret == string("9"));
    ExpectSuccess(api.GetData(name, "oom_killed", ret));
    Expect(ret == string("false"));
    ExpectFailure(api.GetData(name, "start_errno", ret), EError::InvalidState);
    ExpectSuccess(api.Stop(name));

    Say() << "Check oom_killed property" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.SetProperty(name, "cwd", ""));
    ExpectSuccess(api.SetProperty(name, "memory_limit", "10"));
    ExpectSuccess(api.Start(name));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "exit_status", ret));
    Expect(ret == string("9"));
    ExpectSuccess(api.GetData(name, "oom_killed", ret));
    Expect(ret == string("true"));

    ExpectSuccess(api.Destroy(name));
}

static void TestStreams(TPortoAPI &api) {
    string ret;

    string name = "a";
    ExpectSuccess(api.Create(name));

    Say() << "Make sure stdout works" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "bash -c 'echo out >&1'"));
    ExpectSuccess(api.Start(name));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "stdout", ret));
    Expect(ret == string("out\n"));
    ExpectSuccess(api.GetData(name, "stderr", ret));
    Expect(ret == string(""));
    ExpectSuccess(api.Stop(name));

    Say() << "Make sure stderr works" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "bash -c 'echo err >&2'"));
    ExpectSuccess(api.Start(name));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "stdout", ret));
    Expect(ret == string(""));
    ExpectSuccess(api.GetData(name, "stderr", ret));
    Expect(ret == string("err\n"));
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.Destroy(name));
}

static void TestLongRunning(TPortoAPI &api) {
    string pid;

    string name = "a";
    ExpectSuccess(api.Create(name));

    Say() << "Spawn long running task" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));
    Expect(TaskRunning(api, pid) == true);

    Say() << "Check that task namespaces are correct" << std::endl;
    Expect(GetNamespace("self", "pid") != GetNamespace(pid, "pid"));
    Expect(GetNamespace("self", "mnt") != GetNamespace(pid, "mnt"));
    Expect(GetNamespace("self", "ipc") == GetNamespace(pid, "ipc"));
    Expect(GetNamespace("self", "net") == GetNamespace(pid, "net"));
    //Expect(GetNamespace("self", "user") == GetNamespace(pid, "user"));
    Expect(GetNamespace("self", "uts") == GetNamespace(pid, "uts"));

    Say() << "Check that task cgroups are correct" << std::endl;
    auto cgmap = GetCgroups("self");
    for (auto name : cgmap) {
        // skip systemd cgroups
        if (name.first.find("systemd") != string::npos)
            continue;

        Expect(name.second == "/");
    }

    ExpectCorrectCgroups(pid, name);

    string root_cls = GetCgKnob("net_cls", "/", "net_cls.classid");
    string leaf_cls = GetCgKnob("net_cls", name, "net_cls.classid");

    Expect(root_cls != "0");
    Expect(leaf_cls != "0");
    Expect(root_cls != leaf_cls);

    Expect(TcClassExist(root_cls) == true);
    Expect(TcClassExist(leaf_cls) == true);

    ExpectSuccess(api.Stop(name));
    Expect(TaskRunning(api, pid) == false);
    Expect(TcClassExist(leaf_cls) == false);

    Say() << "Check that destroying container removes tclass" << std::endl;
    ExpectSuccess(api.Start(name));
    Expect(TcClassExist(root_cls) == true);
    Expect(TcClassExist(leaf_cls) == true);
    ExpectSuccess(api.Destroy(name));
    Expect(TaskRunning(api, pid) == false);
    Expect(TcClassExist(leaf_cls) == false);
    ExpectSuccess(api.Create(name));

    Say() << "Check that hierarchical task cgroups are correct" << std::endl;

    string child = name + "/b";
    ExpectSuccess(api.Create(child));

    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));
    ExpectCorrectCgroups(pid, name);

    ExpectSuccess(api.SetProperty(child, "command", "sleep 1000"));
    ExpectSuccess(api.Start(child));
    ExpectSuccess(api.GetData(child, "root_pid", pid));
    ExpectCorrectCgroups(pid, child);

    string parent;
    ExpectSuccess(api.GetData(child, "parent", parent));
    Expect(parent == name);

    ExpectSuccess(api.Destroy(child));
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.Destroy(name));
}

static void TestIsolation(TPortoAPI &api) {
    string ret;

    string name = "a";
    ExpectSuccess(api.Create(name));

    Say() << "Make sure PID isolation works" << std::endl;
    ExpectSuccess(api.SetProperty(name, "isolate", "false"));

    ExpectSuccess(api.SetProperty(name, "command", "bash -c 'echo $BASHPID'"));
    ExpectSuccess(api.Start(name));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "stdout", ret));
    Expect(ret != string("1\n"));
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.SetProperty(name, "command", "ps aux"));
    ExpectSuccess(api.Start(name));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "stdout", ret));
    Expect(std::count(ret.begin(), ret.end(), '\n') != 2);
    ExpectSuccess(api.Stop(name));


    ExpectSuccess(api.SetProperty(name, "isolate", "true"));
    ExpectSuccess(api.SetProperty(name, "command", "bash -c 'echo $BASHPID'"));
    ExpectSuccess(api.Start(name));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "stdout", ret));
    Expect(ret == string("1\n"));
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.SetProperty(name, "command", "ps aux"));
    ExpectSuccess(api.Start(name));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "stdout", ret));
    Expect(std::count(ret.begin(), ret.end(), '\n') == 2);

    Say() << "Make sure container has correct network class" << std::endl;

    TNetlink nl;
    Expect(nl.Open() == TError::Success());
    string handle = GetCgKnob("net_cls", name, "net_cls.classid");
    Expect(handle != "0");
    Expect(nl.ClassExists(stoul(handle)) == true);
    ExpectSuccess(api.Stop(name));
    Expect(nl.ClassExists(stoul(handle)) == false);

    ExpectSuccess(api.Destroy(name));
}

static void TestEnvironment(TPortoAPI &api) {
    string pid;

    string name = "a";
    ExpectSuccess(api.Create(name));

    Say() << "Check default environment" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));

    string env = GetEnv(pid);
    static const char empty_env[] = "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/home/nobody\0"
        "HOME=/home/nobody\0"
        "USER=nobody\0";

    Expect(memcmp(empty_env, env.data(), sizeof(empty_env)) == 0);
    ExpectSuccess(api.Stop(name));

    Say() << "Check user-defined environment" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.SetProperty(name, "env", "a=b;c=d;"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));

    env = GetEnv(pid);
    static const char ab_env[] = "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/home/nobody\0"
        "a=b\0"
        "c=d\0"
        "HOME=/home/nobody\0"
        "USER=nobody\0";

    Expect(memcmp(ab_env, env.data(), sizeof(ab_env)) == 0);
    ExpectSuccess(api.Stop(name));


    ExpectSuccess(api.SetProperty(name, "env", "a=b;;c=d;"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));

    env = GetEnv(pid);
    Expect(memcmp(ab_env, env.data(), sizeof(ab_env)) == 0);
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.SetProperty(name, "env", "a=e\\;b;c=d;"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));

    env = GetEnv(pid);
    static const char asb_env[] = "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/home/nobody\0"
        "a=e;b\0"
        "c=d\0"
        "HOME=/home/nobody\0"
        "USER=nobody\0";

    Expect(memcmp(asb_env, env.data(), sizeof(asb_env)) == 0);
    ExpectSuccess(api.Stop(name));


    ExpectSuccess(api.SetProperty(name, "command", "sleep $N"));
    ExpectSuccess(api.SetProperty(name, "env", "N=1"));
    ExpectSuccess(api.Start(name));


    ExpectSuccess(api.Destroy(name));
}

static void TestUserGroup(TPortoAPI &api) {
    int uid, gid;
    string pid;

    string name = "a";
    ExpectSuccess(api.Create(name));

    Say() << "Check default user & group" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));

    GetUidGid(pid, uid, gid);

    Expect(uid == UserUid(GetDefaultUser()));
    Expect(gid == GroupGid(GetDefaultGroup()));
    ExpectSuccess(api.Stop(name));

    Say() << "Check custom user & group" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.SetProperty(name, "user", "daemon"));
    ExpectSuccess(api.SetProperty(name, "group", "bin"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));

    GetUidGid(pid, uid, gid);

    Expect(uid == UserUid("daemon"));
    Expect(gid == GroupGid("bin"));
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.Destroy(name));
}

static void TestCwd(TPortoAPI &api) {
    string pid;
    string cwd;
    string portodPid, portodCwd;

    string name = "a";
    ExpectSuccess(api.Create(name));

    TFile portod(config().slave_pid().path());
    (void)portod.AsString(portodPid);
    portodCwd = GetCwd(portodPid);

    Say() << "Check default working directory" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));
    cwd = GetCwd(pid);

    string prefix = CONTAINER_TMP_DIR;

    Expect(cwd != portodCwd);
    Expect(cwd.length() == prefix.length() + 7);
    Expect(cwd.substr(0, prefix.length()) == prefix);

    Expect(access(cwd.c_str(), F_OK) == 0);
    ExpectSuccess(api.Stop(name));
    Expect(access(cwd.c_str(), F_OK) != 0);
    ExpectSuccess(api.Destroy(name));

    ExpectSuccess(api.Create("b"));
    ExpectSuccess(api.SetProperty("b", "command", "sleep 1000"));
    ExpectSuccess(api.Start("b"));
    ExpectSuccess(api.GetData("b", "root_pid", pid));
    string bcwd = GetCwd(pid);
    ExpectSuccess(api.Destroy("b"));

    Expect(bcwd != portodCwd);
    Expect(bcwd.length() == prefix.length() + 7);
    Expect(bcwd.substr(0, prefix.length()) == prefix);
    Expect(bcwd != cwd);

    Say() << "Check user defined working directory" << std::endl;
    ExpectSuccess(api.Create(name));
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.SetProperty(name, "cwd", "/tmp"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));

    Expect(access("/tmp/stdout", F_OK) == 0);
    Expect(access("/tmp/stderr", F_OK) == 0);

    cwd = GetCwd(pid);

    Expect(cwd == "/tmp");
    Expect(access("/tmp", F_OK) == 0);
    ExpectSuccess(api.Stop(name));
    ExpectSuccess(api.SetProperty(name, "cwd", ""));
    Expect(access("/tmp", F_OK) == 0);

    ExpectSuccess(api.Destroy(name));
}

/*
static void TestRootProperty(TPortoAPI &api) {
    string pid;

    string name = "a";
    ExpectSuccess(api.Create(name));

    Say() << "Check filesystem isolation" << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "pwd"));
    ExpectSuccess(api.SetProperty(name, "cwd", ""));
    ExpectSuccess(api.SetProperty(name, "root", "/root/chroot"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));

    string cwd = GetCwd(pid);
    WaitExit(api, pid);

    Expect(cwd == "/root/chroot");

    string v;
    ExpectSuccess(api.GetData(name, "stdout", v));
    Expect(v == string("/\n"));

    // TODO: check /proc/<PID>/root
    ExpectSuccess(api.Destroy(name));
}
*/

static void TestStateMachine(TPortoAPI &api) {
    string name = "a";
    string pid;
    string v;

    ExpectSuccess(api.Create(name));
    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == "stopped");

    ExpectSuccess(api.SetProperty(name, "command", "sleep 1"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == "running");

    ExpectFailure(api.Start(name), EError::InvalidState);

    ExpectSuccess(api.GetData(name, "root_pid", pid));
    WaitExit(api, pid);
    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == "dead");

    ExpectFailure(api.Start(name), EError::InvalidState);

    ExpectSuccess(api.Stop(name));
    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == "stopped");

    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.Stop(name));
    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == "stopped");

    ExpectSuccess(api.SetProperty(name, "command", "bash -c 'while :; do :; done'"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "root_pid", pid));
    v = GetState(pid);
    Expect(v == "R");

    ExpectSuccess(api.Pause(name));
    v = GetState(pid);
    Expect(v == "D");

    ExpectFailure(api.Pause(name), EError::InvalidState);

    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == "paused");

    ExpectSuccess(api.Resume(name));
    v = GetState(pid);
    Expect(v == "R");

    ExpectFailure(api.Resume(name), EError::InvalidState);

    ExpectSuccess(api.Stop(name));
    Expect(TaskRunning(api, pid) == false);

    Say() << "Make sure we can stop unintentionally frozen container " << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));

    v = GetFreezer(name);
    Expect(v == "THAWED\n");

    SetFreezer(name, "FROZEN");

    v = GetFreezer(name);
    Expect(v == "FROZEN\n");

    ExpectSuccess(api.Stop(name));

    Say() << "Make sure we can remove paused container " << std::endl;
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.Pause(name));
    ExpectSuccess(api.Destroy(name));

    Say() << "Make sure kill works " << std::endl;
    ExpectSuccess(api.Create(name));
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));

    // if container init process doesn't have custom handler for a signal
    // it's ignored
    ExpectSuccess(api.Kill(name, SIGTERM));
    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == "running");

    ExpectSuccess(api.Kill(name, SIGKILL));
    ExpectSuccess(api.GetData(name, "root_pid", v));
    WaitExit(api, v);
    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == "dead");

    // we can't kill root or non-running container
    ExpectFailure(api.Kill(name, SIGKILL), EError::InvalidState);
    ExpectFailure(api.Kill("/", SIGKILL), EError::InvalidState);

    ExpectSuccess(api.Destroy(name));
}

static void TestRoot(TPortoAPI &api) {
    string v;
    string root = "/";
    vector<string> properties = {
        "command",
        "user",
        "group",
        "env",
        "cwd",
        "memory_guarantee",
        "memory_limit",
        "cpu_policy",
        "cpu_priority",
        "net_guarantee",
        "net_ceil",
        "net_priority",
        "respawn",
        "isolate",
    };

    std::vector<TProperty> plist;

    ExpectSuccess(api.Plist(plist));
    Expect(plist.size() == properties.size());

    Say() << "Check root properties & data" << std::endl;
    for (auto p : properties)
        ExpectFailure(api.GetProperty(root, p, v), EError::InvalidProperty);

    ExpectSuccess(api.GetData(root, "state", v));
    Expect(v == string("running"));
    ExpectFailure(api.GetData(root, "exit_status", v), EError::InvalidData);
    ExpectFailure(api.GetData(root, "start_errno", v), EError::InvalidData);
    ExpectFailure(api.GetData(root, "root_pid", v), EError::InvalidData);
    ExpectFailure(api.GetData(root, "stdout", v), EError::InvalidData);
    ExpectFailure(api.GetData(root, "parent", v), EError::InvalidData);
    ExpectFailure(api.GetData(root, "stderr", v), EError::InvalidData);

    ExpectFailure(api.Stop(root), EError::InvalidState);
    ExpectFailure(api.Destroy(root), EError::InvalidValue);

    Say() << "Check root cpu_usage & memory_usage" << std::endl;
    ExpectSuccess(api.GetData(root, "cpu_usage", v));
    Expect(v == "0");
    ExpectSuccess(api.GetData(root, "memory_usage", v));
    Expect(v == "0");
    ExpectSuccess(api.GetData(root, "net_bytes", v));
    Expect(v == "0");
    ExpectSuccess(api.GetData(root, "net_packets", v));
    Expect(v == "0");
    ExpectSuccess(api.GetData(root, "net_drops", v));
    Expect(v == "0");
    ExpectSuccess(api.GetData(root, "net_overlimits", v));
    Expect(v == "0");

    uint32_t defClass = TcHandle(1, 2);
    uint32_t rootClass = TcHandle(1, 1);
    uint32_t nextClass = TcHandle(1, 3);

    uint32_t rootQdisc = TcHandle(1, 0);
    uint32_t nextQdisc = TcHandle(2, 0);

    TNetlink nl;
    Expect(nl.Open() == TError::Success());
    Expect(nl.QdiscExists(rootQdisc) == true);
    Expect(nl.QdiscExists(nextQdisc) == false);
    Expect(nl.ClassExists(defClass) == true);
    Expect(nl.ClassExists(rootClass) == true);
    Expect(nl.ClassExists(nextClass) == false);

    Expect(nl.CgroupFilterExists(rootQdisc, 1) == true);
    Expect(nl.CgroupFilterExists(rootQdisc, 2) == false);
}

static void TestStats(TPortoAPI &api) {
    // should be executed right after TestRoot because assumes empty statistics

    string root = "/";
    string wget = "a";
    string noop = "b";

    ExpectSuccess(api.Create(noop));
    ExpectSuccess(api.SetProperty(noop, "command", "ls -la /bin"));
    ExpectSuccess(api.Start(noop));
    WaitState(api, noop, "dead");

    ExpectSuccess(api.Create(wget));
    ExpectSuccess(api.SetProperty(wget, "command", "wget yandex.ru"));
    ExpectSuccess(api.Start(wget));
    WaitState(api, wget, "dead");

    string v, rv;

    ExpectSuccess(api.GetData(root, "cpu_usage", v));
    Expect(v != "0" && v != "-1");
    ExpectSuccess(api.GetData(root, "memory_usage", v));
    Expect(v != "0" && v != "-1");

    ExpectSuccess(api.GetData(wget, "cpu_usage", v));
    Expect(v != "0" && v != "-1");
    ExpectSuccess(api.GetData(wget, "memory_usage", v));
    Expect(v != "0" && v != "-1");

    ExpectSuccess(api.GetData(noop, "cpu_usage", v));
    Expect(v != "0" && v != "-1");
    ExpectSuccess(api.GetData(noop, "memory_usage", v));
    Expect(v != "0" && v != "-1");

    ExpectSuccess(api.GetData(root, "net_bytes", rv));
    Expect(rv != "0" && rv != "-1");
    ExpectSuccess(api.GetData(wget, "net_bytes", v));
    Expect(v == rv);
    ExpectSuccess(api.GetData(noop, "net_bytes", v));
    Expect(v == "0");
    ExpectSuccess(api.GetData(root, "net_packets", rv));
    Expect(rv != "0" && rv != "-1");
    ExpectSuccess(api.GetData(wget, "net_packets", v));
    Expect(v == rv);
    ExpectSuccess(api.GetData(noop, "net_packets", v));
    Expect(v == "0");
    ExpectSuccess(api.GetData(root, "net_drops", rv));
    Expect(rv == "0");
    ExpectSuccess(api.GetData(wget, "net_drops", v));
    Expect(v == rv);
    ExpectSuccess(api.GetData(noop, "net_drops", v));
    Expect(v == "0");
    ExpectSuccess(api.GetData(root, "net_overlimits", rv));
    Expect(rv == "0");
    ExpectSuccess(api.GetData(wget, "net_overlimits", v));
    Expect(v == rv);
    ExpectSuccess(api.GetData(noop, "net_overlimits", v));
    Expect(v == "0");

    ExpectSuccess(api.Destroy(wget));
    ExpectSuccess(api.Destroy(noop));
}

static bool CanTestLimits() {
    if (!HaveCgKnob("memory", "memory.low_limit_in_bytes"))
        return false;

    if (!HaveCgKnob("memory", "memory.recharge_on_pgfault"))
        return false;

    if (!HaveCgKnob("cpu", "cpu.smart"))
        return false;

    return true;
}

static void TestLimits(TPortoAPI &api) {
    string name = "a";
    ExpectSuccess(api.Create(name));

    Say() << "Check default limits" << std::endl;
    string current;

    current = GetCgKnob("memory", "", "memory.use_hierarchy");
    Expect(current == "1");

    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));

    current = GetCgKnob("memory", name, "memory.use_hierarchy");
    Expect(current == "1");

    current = GetCgKnob("memory", name, "memory.limit_in_bytes");
    Expect(current == std::to_string(LLONG_MAX) || current == std::to_string(ULLONG_MAX));

    if (HaveCgKnob("memory", "memory.low_limit_in_bytes")) {
        current = GetCgKnob("memory", name, "memory.low_limit_in_bytes");
        Expect(current == "0");
    }
    ExpectSuccess(api.Stop(name));

    Say() << "Check custom limits" << std::endl;
    string exp_limit = "524288";
    string exp_guar = "16384";
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.SetProperty(name, "memory_limit", exp_limit));
    if (HaveCgKnob("memory", "memory.low_limit_in_bytes"))
        ExpectSuccess(api.SetProperty(name, "memory_guarantee", exp_guar));
    ExpectSuccess(api.Start(name));

    current = GetCgKnob("memory", name, "memory.limit_in_bytes");
    Expect(current == exp_limit);
    if (HaveCgKnob("memory", "memory.low_limit_in_bytes")) {
        current = GetCgKnob("memory", name, "memory.low_limit_in_bytes");
        Expect(current == exp_guar);
    }
    ExpectSuccess(api.Stop(name));

    Say() << "Check cpu_priority" << std::endl;
    ExpectFailure(api.SetProperty(name, "cpu_priority", "-1"), EError::InvalidValue);
    ExpectFailure(api.SetProperty(name, "cpu_priority", "100"), EError::InvalidValue);
    ExpectSuccess(api.SetProperty(name, "cpu_priority", "0"));
    ExpectSuccess(api.SetProperty(name, "cpu_priority", "99"));

    Say() << "Check cpu_policy" << std::endl;
    string smart;

    ExpectFailure(api.SetProperty(name, "cpu_policy", "somecrap"), EError::InvalidValue);
    ExpectFailure(api.SetProperty(name, "cpu_policy", "idle"), EError::NotSupported);

    if (HaveCgKnob("cpu", "cpu.smart")) {
        ExpectSuccess(api.SetProperty(name, "cpu_policy", "rt"));
        ExpectSuccess(api.Start(name));
        smart = GetCgKnob("cpu", name, "cpu.smart");
        Expect(smart == "1");
        ExpectSuccess(api.Stop(name));

        ExpectSuccess(api.SetProperty(name, "cpu_policy", "normal"));
        ExpectSuccess(api.Start(name));
        smart = GetCgKnob("cpu", name, "cpu.smart");
        Expect(smart == "0");
        ExpectSuccess(api.Stop(name));
    }

    string shares;
    ExpectSuccess(api.SetProperty(name, "cpu_policy", "normal"));
    ExpectSuccess(api.SetProperty(name, "cpu_priority", "0"));
    ExpectSuccess(api.Start(name));
    shares = GetCgKnob("cpu", name, "cpu.shares");
    Expect(shares == "2");
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.SetProperty(name, "cpu_priority", "50"));
    ExpectSuccess(api.Start(name));
    shares = GetCgKnob("cpu", name, "cpu.shares");
    Expect(shares == "52");
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.SetProperty(name, "cpu_priority", "99"));
    ExpectSuccess(api.Start(name));
    shares = GetCgKnob("cpu", name, "cpu.shares");
    Expect(shares == "101");
    ExpectSuccess(api.Stop(name));

    uint32_t netGuarantee = 100000, netCeil = 200000, netPrio = 4;
    ExpectSuccess(api.SetProperty(name, "net_guarantee", std::to_string(netGuarantee)));
    ExpectSuccess(api.SetProperty(name, "net_ceil", std::to_string(netCeil)));
    ExpectFailure(api.SetProperty(name, "net_priority", "-1"), EError::InvalidValue);
    ExpectFailure(api.SetProperty(name, "net_priority", "8"), EError::InvalidValue);
    ExpectSuccess(api.SetProperty(name, "net_priority", "0"));
    ExpectSuccess(api.SetProperty(name, "net_priority", std::to_string(netPrio)));
    ExpectSuccess(api.Start(name));

    uint32_t prio, rate, ceil;
    TNetlink nl;
    Expect(nl.Open() == TError::Success());
    string handle = GetCgKnob("net_cls", name, "net_cls.classid");
    ExpectSuccess(nl.GetClassProperties(stoul(handle), prio, rate, ceil));

    Expect(prio == netPrio);
    Expect(rate == netGuarantee);
    Expect(ceil == netCeil);
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.Destroy(name));
}

static void TestRawLimits(TPortoAPI &api) {
    string name = "a";
    ExpectSuccess(api.Create(name));

    Say() << "Check default limits" << std::endl;
    string current;

    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));

    current = GetCgKnob("memory", name, "memory.limit_in_bytes");
    Expect(current == std::to_string(LLONG_MAX) || current == std::to_string(ULLONG_MAX));

    if (HaveCgKnob("memory", "memory.low_limit_in_bytes")) {
        current = GetCgKnob("memory", name, "memory.low_limit_in_bytes");
        Expect(current == "0");
    }

    if (HaveCgKnob("memory", "memory.recharge_on_pgfault")) {
        current = GetCgKnob("memory", name, "memory.recharge_on_pgfault");
        Expect(current == "0");
    }

    if (HaveCgKnob("cpu", "cpu.smart")) {
        current = GetCgKnob("cpu", name, "cpu.smart");
        Expect(current == "0");
    }
    ExpectSuccess(api.Stop(name));

    Say() << "Check custom limits" << std::endl;
    string exp_limit = "524288";
    string exp_guar = "16384";
    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.SetProperty(name, "memory.limit_in_bytes", exp_limit));
    if (HaveCgKnob("memory", "memory.low_limit_in_bytes"))
        ExpectSuccess(api.SetProperty(name, "memory.low_limit_in_bytes", exp_guar));
    if (HaveCgKnob("memory", "memory.recharge_on_pgfault"))
        ExpectSuccess(api.SetProperty(name, "memory.recharge_on_pgfault", "1"));
    if (HaveCgKnob("cpu", "cpu.smart"))
        ExpectSuccess(api.SetProperty(name, "cpu.smart", "1"));
    ExpectSuccess(api.Start(name));

    current = GetCgKnob("memory", name, "memory.limit_in_bytes");
    Expect(current == exp_limit);
    if (HaveCgKnob("memory", "memory.low_limit_in_bytes")) {
        current = GetCgKnob("memory", name, "memory.low_limit_in_bytes");
        Expect(current == exp_guar);
    }
    if (HaveCgKnob("memory", "memory.recharge_on_pgfault")) {
        current = GetCgKnob("memory", name, "memory.recharge_on_pgfault");
        Expect(current == "1");
    }
    if (HaveCgKnob("cpu", "cpu.smart")) {
        current = GetCgKnob("cpu", name, "cpu.smart");
        Expect(current == "1");
    }
    ExpectSuccess(api.Stop(name));
    ExpectSuccess(api.Destroy(name));
}

static void TestDynamic(TPortoAPI &api) {
    string name = "a";
    ExpectSuccess(api.Create(name));

    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));

    string current;
    current = GetCgKnob("memory", name, "memory.limit_in_bytes");
    Expect(current == std::to_string(LLONG_MAX) || current == std::to_string(ULLONG_MAX));

    string exp_limit = "268435456";
    ExpectSuccess(api.SetProperty(name, "memory_limit", exp_limit));
    current = GetCgKnob("memory", name, "memory.limit_in_bytes");
    Expect(current == exp_limit);

    ExpectSuccess(api.Pause(name));

    exp_limit = "536870912";
    ExpectSuccess(api.SetProperty(name, "memory_limit", exp_limit));
    current = GetCgKnob("memory", name, "memory.limit_in_bytes");
    Expect(current == exp_limit);

    ExpectSuccess(api.Resume(name));
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.Destroy(name));
}

static void TestLimitsHierarchy(TPortoAPI &api) {
    if (!HaveCgKnob("memory", "memory.low_limit_in_bytes"))
        return;

    //
    // box +-- monitoring
    //     |
    //     +-- system
    //     |
    //     +-- production +-- slot1
    //                    |
    //                    +-- slot2
    //

    string box = "box";
    string prod = "box/production";
    string slot1 = "box/production/slot1";
    string slot2 = "box/production/slot2";
    string system = "box/system";
    string monit = "box/monitoring";

    ExpectSuccess(api.Create(box));
    ExpectSuccess(api.Create(prod));
    ExpectSuccess(api.Create(slot1));
    ExpectSuccess(api.Create(slot2));
    ExpectSuccess(api.Create(system));
    ExpectSuccess(api.Create(monit));

    size_t total = GetTotalMemory();

    Say() << "Single container can't go over reserve" << std::endl;
    ExpectFailure(api.SetProperty(system, "memory_guarantee", std::to_string(total)), EError::ResourceNotAvailable);
    ExpectSuccess(api.SetProperty(system, "memory_guarantee", std::to_string(total - config().daemon().memory_guarantee_reserve())));

    Say() << "Distributed guarantee can't go over reserve" << std::endl;
    size_t chunk = (total - config().daemon().memory_guarantee_reserve()) / 4;

    ExpectSuccess(api.SetProperty(system, "memory_guarantee", std::to_string(chunk)));
    ExpectSuccess(api.SetProperty(monit, "memory_guarantee", std::to_string(chunk)));
    ExpectSuccess(api.SetProperty(slot1, "memory_guarantee", std::to_string(chunk)));
    ExpectFailure(api.SetProperty(slot2, "memory_guarantee", std::to_string(chunk + 1)), EError::ResourceNotAvailable);
    ExpectSuccess(api.SetProperty(slot2, "memory_guarantee", std::to_string(chunk)));

    ExpectSuccess(api.SetProperty(monit, "memory_guarantee", std::to_string(0)));
    ExpectSuccess(api.SetProperty(system, "memory_guarantee", std::to_string(0)));

    auto CheckPropertyHierarhcy = [&](TPortoAPI &api, const std::string &property) {
        Say() << "Parent can't have less guarantee than sum of children" << std::endl;
        ExpectSuccess(api.SetProperty(slot1, property, std::to_string(chunk)));
        ExpectSuccess(api.SetProperty(slot2, property, std::to_string(chunk)));
        ExpectFailure(api.SetProperty(prod, property, std::to_string(chunk)), EError::InvalidValue);
        ExpectFailure(api.SetProperty(box, property, std::to_string(chunk)), EError::InvalidValue);

        Say() << "Child can't go over parent guarantee" << std::endl;
        ExpectSuccess(api.SetProperty(prod, property, std::to_string(2 * chunk)));
        ExpectFailure(api.SetProperty(slot1, property, std::to_string(2 * chunk)), EError::InvalidValue);

        Say() << "Can lower guarantee if possible" << std::endl;
        ExpectFailure(api.SetProperty(prod, property, std::to_string(chunk)), EError::InvalidValue);
        ExpectSuccess(api.SetProperty(slot2, property, std::to_string(0)));
        ExpectSuccess(api.SetProperty(prod, property, std::to_string(chunk)));
    };

    CheckPropertyHierarhcy(api, "memory_guarantee");
    CheckPropertyHierarhcy(api, "memory_limit");

    ExpectSuccess(api.Destroy(monit));
    ExpectSuccess(api.Destroy(system));
    ExpectSuccess(api.Destroy(slot2));
    ExpectSuccess(api.Destroy(slot1));
    ExpectSuccess(api.Destroy(prod));
    ExpectSuccess(api.Destroy(box));
}

static void TestPermissions(TPortoAPI &api) {
    struct stat st;
    string path;

    string name = "a";
    ExpectSuccess(api.Create(name));

    ExpectSuccess(api.SetProperty(name, "command", "sleep 1000"));
    ExpectSuccess(api.Start(name));

    path = "/sys/fs/cgroup/memory/porto";
    Expect(lstat(path.c_str(), &st) == 0);
    Expect(st.st_mode == (0755 | S_IFDIR));

    path = "/sys/fs/cgroup/memory/porto/" + name;
    Expect(lstat(path.c_str(), &st) == 0);
    Expect(st.st_mode == (0755 | S_IFDIR));

    path = "/sys/fs/cgroup/memory/porto/" + name + "/tasks";
    Expect(lstat(path.c_str(), &st) == 0);
    Expect(st.st_mode == (0644 | S_IFREG));

    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.Destroy(name));
}

static void TestRespawn(TPortoAPI &api) {
    string pid, respawnPid;
    string ret;

    string name = "a";
    ExpectSuccess(api.Create(name));

    ExpectSuccess(api.SetProperty(name, "command", "sleep 1"));

    ExpectSuccess(api.SetProperty(name, "respawn", "false"));
    ExpectSuccess(api.Start(name));
    ExpectSuccess(api.GetData(name, "respawn_count", ret));
    Expect(ret == string("0"));
    WaitState(api, name, "dead");
    ExpectSuccess(api.GetData(name, "respawn_count", ret));
    Expect(ret == string("0"));
    ExpectSuccess(api.Stop(name));

    ExpectSuccess(api.SetProperty(name, "respawn", "true"));
    ExpectSuccess(api.Start(name));

    ExpectSuccess(api.GetData(name, "root_pid", pid));
    WaitExit(api, pid);
    ExpectSuccess(api.GetData(name, "root_pid", respawnPid));
    Expect(pid != respawnPid);
    ExpectSuccess(api.GetData(name, "respawn_count", ret));
    Expect(ret != "0" && ret != "");

    ExpectSuccess(api.Stop(name));
    ExpectSuccess(api.SetProperty(name, "respawn", "false"));

    ExpectSuccess(api.Destroy(name));
}

static int LeakConainersNr;
static void TestLeaks(TPortoAPI &api) {
    string pid;
    string name;
    int slack = 4096;

    TFile f(config().slave_pid().path());
    Expect(f.AsString(pid) == false);

    for (int i = 0; i < LeakConainersNr; i++) {
        name = "a" + std::to_string(i);
        ExpectSuccess(api.Create(name));
        ExpectSuccess(api.SetProperty(name, "command", "true"));
        ExpectSuccess(api.Start(name));
    }

    for (int i = 0; i < LeakConainersNr; i++) {
        name = "a" + std::to_string(i);
        ExpectSuccess(api.Destroy(name));
    }

    int prev = GetVmRss(pid);

    for (int i = 0; i < LeakConainersNr; i++) {
        name = "b" + std::to_string(i);
        ExpectSuccess(api.Create(name));
        ExpectSuccess(api.SetProperty(name, "command", "true"));
        ExpectSuccess(api.Start(name));
    }

    for (int i = 0; i < LeakConainersNr; i++) {
        name = "b" + std::to_string(i);
        ExpectSuccess(api.Destroy(name));
    }

    int now = GetVmRss(pid);
    Say() << "Expected " << now << " < " << prev + slack << std::endl;
    Expect(now <= prev + slack);
}

static void TestRecovery(TPortoAPI &api) {
    string pid, v;
    string name = "a";

    map<string,string> props = {
        { "command", "sleep 1000" },
        { "user", "bin" },
        { "group", "daemon" },
        { "env", "a=a;b=b" },
    };

    Say() << "Make sure we don't kill containers when doing recovery" << std::endl;
    ExpectSuccess(api.Create(name));

    for (auto &pair : props)
        ExpectSuccess(api.SetProperty(name, pair.first, pair.second));
    ExpectSuccess(api.Start(name));

    ExpectSuccess(api.GetData(name, "root_pid", pid));
    Expect(TaskRunning(api, pid) == true);
    Expect(TaskZombie(api, pid) == false);

    int portodPid = ReadPid(config().slave_pid().path());
    if (kill(portodPid, SIGKILL))
        throw string("Can't send SIGKILL to slave");

    WaitExit(api, std::to_string(portodPid));
    WaitPortod(api);

    ExpectSuccess(api.GetData(name, "state", v));
    Expect(v == "running");
    ExpectSuccess(api.GetData(name, "root_pid", v));
    Expect(v == pid);

    Expect(TaskRunning(api, pid) == true);
    Expect(TaskZombie(api, pid) == false);

    for (auto &pair : props) {
        string v;
        ExpectSuccess(api.GetProperty(name, pair.first, v));
        Expect(v == pair.second);
    }

    ExpectSuccess(api.Destroy(name));

    Say() << "Make sure hierarchical recovery works" << std::endl;

    string parent = "a";
    string child = "a/b";
    ExpectSuccess(api.Create(parent));
    ExpectSuccess(api.Create(child));

    portodPid = ReadPid(config().slave_pid().path());
    if (kill(portodPid, SIGKILL))
        throw string("Can't send SIGKILL to slave");
    WaitExit(api, std::to_string(portodPid));
    WaitPortod(api);

    std::vector<std::string> containers;

    ExpectSuccess(api.List(containers));
    Expect(containers.size() == 3);
    Expect(containers[0] == string("/"));
    Expect(containers[1] == string("a"));
    Expect(containers[2] == string("a/b"));
    ExpectSuccess(api.Destroy(child));
    ExpectSuccess(api.Destroy(parent));
}

static void TestCgroups(TPortoAPI &api) {
    string cg = "/sys/fs/cgroup/freezer/qwerty/asdfg";

    TFolder f(cg);
    if (f.Exists())
        Expect(f.Remove() == false);
    Expect(f.Create(0755, true) == false);

    int pid = ReadPid(config().slave_pid().path());
    if (kill(pid, SIGINT))
        throw string("Can't send SIGINT to slave");

    WaitPortod(api);

    pid = ReadPid(config().slave_pid().path());
    if (kill(pid, SIGINT))
        throw string("Can't send SIGINT to slave");

    WaitPortod(api);

    Expect(f.Exists() == true);
    Expect(f.Remove() == false);
}

int SelfTest(string name, int leakNr) {
    pair<string, std::function<void(TPortoAPI &)>> tests[] = {
        { "root", TestRoot },
        { "stats", TestStats },
        { "holder", TestHolder },
        { "empty", TestEmpty },
        { "state_machine", TestStateMachine },
        { "exit_status", TestExitStatus },
        { "streams", TestStreams },
        { "long_running", TestLongRunning },
        { "isolation", TestIsolation },
        { "environment", TestEnvironment },
        { "user_group", TestUserGroup },
        { "cwd", TestCwd },
        //{ "root", TestRootProperty },
        { "limits", TestLimits },
        { "raw", TestRawLimits },
        { "dynamic", TestDynamic },
        { "permissions", TestPermissions },
        { "respawn", TestRespawn },
        { "hierarchy", TestLimitsHierarchy },
        { "leaks", TestLeaks },

        { "daemon", TestDaemon },
        { "recovery", TestRecovery },
        { "cgroups", TestCgroups },
    };

    LeakConainersNr = leakNr;

    int respawns = 0;
    int errors = 0;
    try {
        TPortoAPI api;

        RestartDaemon(api);

        Expect(WordCount(config().master_log().path(), "Started") == 1);
        Expect(WordCount(config().slave_log().path(), "Started") == 1);

        for (auto t : tests) {
            if (name.length() && name != t.first)
                continue;

            std::cerr << ">>> Testing " << t.first << "..." << std::endl;
            t.second(api);
        }

        respawns = WordCount(config().master_log().path(), "Spawned");
        errors = WordCount(config().slave_log().path(), "Error");
    } catch (string e) {
        std::cerr << "EXCEPTION: " << e << std::endl;
        return 1;
    }

    std::cerr << "SUCCESS: All tests successfully passed!" << std::endl;
    if (!CanTestLimits())
        std::cerr << "WARNING: Due to missing kernel support, memory_guarantee/cpu_policy has not been tested!" << std::endl;
    if (respawns != 1 /* start */ + 2 /* TestRecovery */ + 2 /* TestCgroups */)
        std::cerr << "WARNING: Unexpected number of respawns: " << respawns << "!" << std::endl;
    if (errors !=
        4 + /* Invalid command */
        6 + /* Invalid property value */
        2 + /* Memory guarantee */
        8 + /* Hierarchical properties */
        3 /* Can't remove cgroups */)
        std::cerr << "WARNING: Unexpected number of errors: " << errors << "!" << std::endl;

    return 0;
}
}
