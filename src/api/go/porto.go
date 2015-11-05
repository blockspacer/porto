package porto

import (
	"encoding/binary"
	"errors"
	"io"
	"net"

	"github.com/golang/protobuf/proto"

	"github.com/yandex/porto/src/api/go/rpc"
)

const PortoSocket = "/var/run/portod.socket"

func SendData(conn io.Writer, data []byte) error {
	// First we have to send actual data size,
	// then the data itself
	buf := make([]byte, 64)
	len := binary.PutUvarint(buf, uint64(len(data)))
	_, err := conn.Write(buf[:len])
	if err != nil {
		return err
	}
	_, err = conn.Write(data)
	return err
}

func RecvData(conn io.Reader) ([]byte, error) {
	buf := make([]byte, 1024*1024)

	len, err := conn.Read(buf)
	if err != nil {
		return nil, err
	}

	exp, shift := binary.Uvarint(buf)

	// length of result is exp,
	// so preallocate a buffer for it.
	var ret = make([]byte, exp)
	// bytes after an encoded uint64 and up to len
	// are belong to a packed structure, so copy them
	copy(ret, buf[shift:len])

	// we don't need to check that
	// len > shift, as we ask to read enough data
	// to decode uint64. Otherwise we would have an error before.
	for pos := len - shift; uint64(pos) < exp; {
		n, err := conn.Read(ret[pos:])
		if err != nil {
			return nil, err
		}
		pos += n
	}

	return ret, nil
}

func (conn *PortoConnection) PerformRequest(req *rpc.TContainerRequest) (*rpc.TContainerResponse, error) {
	conn.err = 0
	conn.msg = ""

	data, err := proto.Marshal(req)
	if err != nil {
		return nil, err
	}

	err = SendData(conn.conn, data)
	if err != nil {
		return nil, err
	}

	data, err = RecvData(conn.conn)
	if err != nil {
		return nil, err
	}

	resp := new(rpc.TContainerResponse)

	err = proto.Unmarshal(data, resp)
	if err != nil {
		return nil, err
	}

	conn.err = resp.GetError()
	conn.msg = resp.GetErrorMsg()

	if resp.GetError() != rpc.EError_Success {
		return resp, errors.New(rpc.EError_name[int32(resp.GetError())])
	}

	return resp, nil
}

type TProperty struct {
	Name        string
	Description string
}

type TData struct {
	Name        string
	Description string
}

type TVolumeDescription struct {
	Path       string
	Properties map[string]string
	Containers []string
}

type TPortoGetResponse struct {
	Value    string
	Error    int
	ErrorMsg string
}

type PortoAPI interface {
	GetVersion() (string, string, error)

	GetLastError() rpc.EError
	GetLastErrorMessage() string

	// ContainerAPI
	Create(name string) error
	Destroy(name string) error

	Start(name string) error
	Stop(name string) error
	Kill(name string, sig int) error
	Pause(name string) error
	Resume(name string) error

	Wait(containers []string, timeout int) (string, error)

	List() ([]string, error)
	Plist() ([]TProperty, error)
	Dlist() ([]TData, error)

	Get(containers []string, variables []string) (map[string]map[string]TPortoGetResponse, error)

	GetProperty(name string, property string) (string, error)
	SetProperty(name string, property string, value string) error

	GetData(name string, data string) (string, error)

	// VolumeAPI
	ListVolumeProperties() ([]TProperty, error)
	CreateVolume(path string, config map[string]string) (TVolumeDescription, error)
	LinkVolume(path string, container string) error
	UnlinkVolume(path string, container string) error
	ListVolumes(path string, container string) ([]TVolumeDescription, error)

	// LayerAPI
	ImportLayer(layer string, tarball string, merge bool) error
	ExportLayer(volume string, tarball string) error
	RemoveLayer(layer string) error
	ListLayers(layers []string) error
}

type PortoConnection struct {
	conn net.Conn
	err  rpc.EError
	msg  string
}

func NewPortoConnection() (*PortoConnection, error) {
	c, err := net.Dial("unix", PortoSocket)
	if err != nil {
		return nil, err
	}

	ret := new(PortoConnection)
	ret.conn = c
	return ret, nil
}

func (conn *PortoConnection) Close() error {
	return conn.conn.Close()
}

func (conn *PortoConnection) GetLastError() rpc.EError {
	return conn.err
}

func (conn *PortoConnection) GetLastErrorMessage() string {
	return conn.msg
}

func (conn *PortoConnection) GetVersion() (string, string, error) {
	req := &rpc.TContainerRequest{
		Version: new(rpc.TVersionRequest),
	}
	resp, err := conn.PerformRequest(req)
	if err != nil {
		return "", "", err
	}

	return resp.GetVersion().GetTag(), resp.GetVersion().GetRevision(), nil
}

// ContainerAPI
func (conn *PortoConnection) Create(name string) error {
	req := &rpc.TContainerRequest{
		Create: &rpc.TContainerCreateRequest{
			Name: &name,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) Destroy(name string) error {
	req := &rpc.TContainerRequest{
		Destroy: &rpc.TContainerDestroyRequest{
			Name: &name,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) Start(name string) error {
	req := &rpc.TContainerRequest{
		Start: &rpc.TContainerStartRequest{
			Name: &name,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) Stop(name string) error {
	req := &rpc.TContainerRequest{
		Stop: &rpc.TContainerStopRequest{
			Name: &name,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) Kill(name string, sig int32) error {
	req := &rpc.TContainerRequest{
		Kill: &rpc.TContainerKillRequest{
			Name: &name,
			Sig:  &sig,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) Pause(name string) error {
	req := &rpc.TContainerRequest{
		Pause: &rpc.TContainerPauseRequest{
			Name: &name,
		},
	}
	_, err := conn.PerformRequest(req)
	return err

}

func (conn *PortoConnection) Resume(name string) error {
	req := &rpc.TContainerRequest{
		Resume: &rpc.TContainerResumeRequest{
			Name: &name,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) Wait(containers []string, timeout int) (string, error) {
	req := &rpc.TContainerRequest{
		Wait: &rpc.TContainerWaitRequest{
			Name: containers,
		},
	}
	resp, err := conn.PerformRequest(req)
	if err != nil {
		return "", err
	}

	return resp.GetWait().GetName(), nil
}

func (conn *PortoConnection) List() ([]string, error) {
	req := &rpc.TContainerRequest{
		List: new(rpc.TContainerListRequest),
	}
	resp, err := conn.PerformRequest(req)
	if err != nil {
		return nil, err
	}

	return resp.GetList().GetName(), nil
}

func (conn *PortoConnection) Plist() (ret []TProperty, err error) {
	req := &rpc.TContainerRequest{
		PropertyList: new(rpc.TContainerPropertyListRequest),
	}
	resp, err := conn.PerformRequest(req)
	for _, property := range resp.GetPropertyList().GetList() {
		var p = TProperty{
			Name:        property.GetName(),
			Description: property.GetDesc(),
		}
		ret = append(ret, p)
	}
	return ret, err
}

func (conn *PortoConnection) Dlist() (ret []TData, err error) {
	req := &rpc.TContainerRequest{
		DataList: new(rpc.TContainerDataListRequest),
	}
	resp, err := conn.PerformRequest(req)
	if err != nil {
		return nil, err
	}

	for _, data := range resp.GetDataList().GetList() {
		var p = TData{
			Name:        data.GetName(),
			Description: data.GetDesc(),
		}
		ret = append(ret, p)
	}

	return ret, nil
}

func (conn *PortoConnection) Get(containers []string, variables []string) (ret map[string]map[string]TPortoGetResponse, err error) {
	ret = make(map[string]map[string]TPortoGetResponse)
	req := &rpc.TContainerRequest{
		Get: &rpc.TContainerGetRequest{
			Name:     containers,
			Variable: variables,
		},
	}

	resp, err := conn.PerformRequest(req)
	if err != nil {
		return nil, err
	}

	for _, item := range resp.GetGet().GetList() {
		for _, value := range item.GetKeyval() {
			var v = TPortoGetResponse{
				Value:    value.GetValue(),
				Error:    int(value.GetError()),
				ErrorMsg: value.GetErrorMsg(),
			}

			if _, ok := ret[item.GetName()]; !ok {
				ret[item.GetName()] = make(map[string]TPortoGetResponse)
			}

			ret[item.GetName()][value.GetVariable()] = v
		}
	}
	return ret, err
}

func (conn *PortoConnection) GetProperty(name string, property string) (string, error) {
	req := &rpc.TContainerRequest{
		GetProperty: &rpc.TContainerGetPropertyRequest{
			Name:     &name,
			Property: &property,
		},
	}

	resp, err := conn.PerformRequest(req)
	if err != nil {
		return "", err
	}

	return resp.GetGetProperty().GetValue(), nil
}

func (conn *PortoConnection) SetProperty(name string, property string, value string) error {
	req := &rpc.TContainerRequest{
		SetProperty: &rpc.TContainerSetPropertyRequest{
			Name:     &name,
			Property: &property,
			Value:    &value,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) GetData(name string, data string) (string, error) {
	req := &rpc.TContainerRequest{
		GetData: &rpc.TContainerGetDataRequest{
			Name: &name,
			Data: &data,
		},
	}

	resp, err := conn.PerformRequest(req)
	if err != nil {
		return "", err
	}

	return resp.GetGetData().GetValue(), nil
}

// VolumeAPI
func (conn *PortoConnection) ListVolumeProperties() (ret []TProperty, err error) {
	req := &rpc.TContainerRequest{
		ListVolumeProperties: &rpc.TVolumePropertyListRequest{},
	}

	resp, err := conn.PerformRequest(req)
	if err != nil {
		return nil, err
	}

	for _, property := range resp.GetVolumePropertyList().GetProperties() {
		var desc = TProperty{
			Name:        property.GetName(),
			Description: property.GetDesc(),
		}
		ret = append(ret, desc)
	}
	return ret, err
}

func (conn *PortoConnection) CreateVolume(path string, config map[string]string) (desc TVolumeDescription, err error) {
	var properties []*rpc.TVolumeProperty
	for k, v := range config {
		prop := &rpc.TVolumeProperty{Name: &k, Value: &v}
		properties = append(properties, prop)
	}

	req := &rpc.TContainerRequest{
		CreateVolume: &rpc.TVolumeCreateRequest{
			Path:       &path,
			Properties: properties,
		},
	}

	resp, err := conn.PerformRequest(req)
	if err != nil {
		return desc, err
	}

	volume := resp.GetVolume()
	desc.Path = volume.GetPath()
	desc.Containers = append(desc.Containers, volume.GetContainers()...)
	desc.Properties = make(map[string]string, len(volume.GetProperties()))

	for _, property := range volume.GetProperties() {
		k := property.GetName()
		v := property.GetValue()
		desc.Properties[k] = v
	}

	return desc, err
}

func (conn *PortoConnection) LinkVolume(path string, container string) error {
	req := &rpc.TContainerRequest{
		LinkVolume: &rpc.TVolumeLinkRequest{
			Path:      &path,
			Container: &container,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) UnlinkVolume(path string, container string) error {
	req := &rpc.TContainerRequest{
		UnlinkVolume: &rpc.TVolumeUnlinkRequest{
			Path:      &path,
			Container: &container,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) ListVolumes(path string, container string) (ret []TVolumeDescription, err error) {
	req := &rpc.TContainerRequest{
		ListVolumes: &rpc.TVolumeListRequest{
			Path:      &path,
			Container: &container,
		},
	}

	if path == "" {
		req.ListVolumes.Path = nil
	}
	if container == "" {
		req.ListVolumes.Container = nil
	}

	resp, err := conn.PerformRequest(req)
	if err != nil {
		return nil, err
	}

	for _, volume := range resp.GetVolumeList().GetVolumes() {
		var desc TVolumeDescription
		desc.Path = volume.GetPath()
		desc.Containers = append(desc.Containers, volume.GetContainers()...)
		desc.Properties = make(map[string]string)

		for _, property := range volume.GetProperties() {
			k := property.GetName()
			v := property.GetValue()
			desc.Properties[k] = v
		}
		ret = append(ret, desc)
	}
	return ret, err
}

// LayerAPI
func (conn *PortoConnection) ImportLayer(layer string, tarball string, merge bool) error {
	req := &rpc.TContainerRequest{
		ImportLayer: &rpc.TLayerImportRequest{
			Layer:   &layer,
			Tarball: &tarball,
			Merge:   &merge},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) ExportLayer(volume string, tarball string) error {
	req := &rpc.TContainerRequest{
		ExportLayer: &rpc.TLayerExportRequest{
			Volume:  &volume,
			Tarball: &tarball,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) RemoveLayer(layer string) error {
	req := &rpc.TContainerRequest{
		RemoveLayer: &rpc.TLayerRemoveRequest{
			Layer: &layer,
		},
	}
	_, err := conn.PerformRequest(req)
	return err
}

func (conn *PortoConnection) ListLayers() ([]string, error) {
	req := &rpc.TContainerRequest{
		ListLayers: &rpc.TLayerListRequest{},
	}

	resp, err := conn.PerformRequest(req)
	if err != nil {
		return nil, err
	}

	return resp.GetLayers().GetLayer(), nil
}
