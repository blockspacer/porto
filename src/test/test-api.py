import porto
import sys
import os

def Except(func, *args, **kwargs):
    try:
        func(*args, **kwargs)
    except:
        return sys.exc_info()[0]
    return None

c = porto.Connection()
c.connect()

c.List()
c.Plist()
c.Dlist()
c.Vlist()

c.ListContainers()
c.ListVolumes()
c.ListLayers()

r = c.Find("/")
r.GetData('cpu_usage')

prefix = "test-apy.py-"
container_name = prefix + "a"
layer_name = prefix + "layer"
volume_private = prefix + "volume"
volume_size = 256*(2**20)
volume_size_eps = 40*(2**20) # ext4: 5% reserve + 256 byte inode per 4 of data
volume_path = "/tmp/" + prefix + "layer"
tarball_path = "/tmp/" + prefix + "layer.tgz"

# CLEANUP

if not Except(c.Find, container_name):
    c.Destroy(container_name)

if not Except(c.FindVolume, volume_path):
    c.FindVolume(volume_path).Destroy()

if os.access(volume_path, os.F_OK):
    os.rmdir(volume_path)

for v in c.ListVolumes():
    if v.GetProperties().get("private") == volume_private:
            v.Destroy()

if not Except(c.FindLayer, layer_name):
    c.FindLayer(layer_name).Remove()

if os.access(tarball_path, os.F_OK):
    os.unlink(tarball_path)

assert Except(c.Find, container_name) == porto.exceptions.ContainerDoesNotExist
assert not container_name in c.List()

a = c.Create(container_name)
assert a.name == container_name
assert c.Find(container_name).name == container_name
assert container_name in c.List()

assert a.GetData("state") == "stopped"
a.SetProperty("command", "true")
a.Start()
assert a.Wait() == a.name
assert a.GetData("state") == "dead"
assert a.GetData("exit_status") == "0"
a.Stop()
assert a.GetData("state") == "stopped"

a.SetProperty("command", "sleep 60")
a.Start()
assert a.GetData("state") == "running"
a.Pause()
assert a.GetData("state") == "paused"
a.Resume("")
assert a.GetData("state") == "running"
a.Kill(9)
assert a.Wait() == a.name
assert a.GetData("state") == "dead"
assert a.GetData("exit_status") == "9"
a.Stop()
assert a.GetData("state") == "stopped"

a.SetProperty("command", "echo test")
a.Start()
assert a.Wait() == a.name
assert a.GetData("exit_status") == "0"
assert a.GetData("stdout") == "test\n"
c.Destroy(a)

assert Except(c.Find, container_name) == porto.exceptions.ContainerDoesNotExist
assert not container_name in c.List()

c.ListVolumes()
v = c.CreateVolume(private=volume_private)
v.GetProperties()
f = file(v.path + "/file", 'w')
f.write("test")
f.close()

v.Export(tarball_path)
l = c.ImportLayer(layer_name, tarball_path)
assert l.name == layer_name
assert c.FindLayer(layer_name).name == layer_name

assert Except(c.CreateVolume, volume_path) == porto.exceptions.InvalidValue
os.mkdir(volume_path)
w = c.CreateVolume(volume_path, layers=[layer_name], space_limit=str(volume_size))
assert w.path == volume_path
assert c.FindVolume(volume_path).path == volume_path
assert len(w.GetLayers()) == 1
assert w.GetLayers()[0].name == layer_name
f = file(w.path + "/file", 'r+')
assert f.read() == "test"
assert int(w.GetProperty("space_used")) <= volume_size_eps
assert int(w.GetProperty("space_available")) > volume_size - volume_size_eps
f.write("x" * (volume_size - volume_size_eps * 2))
assert int(w.GetProperty("space_used")) >= volume_size - volume_size_eps * 2
assert int(w.GetProperty("space_available")) < volume_size_eps * 2
assert Except(f.write, "x" * volume_size_eps * 2) == IOError
assert int(w.GetProperty("space_used")) >= volume_size - volume_size_eps
assert int(w.GetProperty("space_available")) < volume_size_eps

a = c.Create(container_name)
w.Link(a)
w.Unlink()
assert len(w.GetContainers()) == 1
assert w.GetContainers()[0].name == container_name
assert Except(l.Remove) == porto.exceptions.Busy
v.Destroy()
assert Except(c.FindVolume, v.path) == porto.exceptions.VolumeNotFound

c.Destroy(a)
assert Except(c.FindVolume, w.path) == porto.exceptions.VolumeNotFound

l.Remove()
os.rmdir(volume_path)
c.disconnect()