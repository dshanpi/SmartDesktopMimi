// Stub implementations for USB storage features (disabled on arm64)

package kvm

import (
	"time"

	"github.com/gin-gonic/gin"
)

const SDMountOK = "OK"

type VirtualMediaUrlInfo struct {
	URL          string `json:"url"`
	ContentType  string `json:"contentType"`
	AuthUsername string `json:"authUsername,omitempty"`
	AuthPassword string `json:"authPassword,omitempty"`
}

type VirtualMediaSource string

type VirtualMediaMode string

const (
	Disk VirtualMediaMode = "Disk"
)

type VirtualMediaState struct {
	Source   VirtualMediaSource `json:"source"`
	Mode     VirtualMediaMode   `json:"mode"`
	Filename string             `json:"filename,omitempty"`
	URL      string             `json:"url,omitempty"`
	Size     int64              `json:"size"`
}

type StorageSpace struct {
	BytesUsed int64 `json:"bytesUsed"`
	BytesFree int64 `json:"bytesFree"`
}

type StorageFile struct {
	Filename  string    `json:"filename"`
	Size      int64     `json:"size"`
	CreatedAt time.Time `json:"createdAt"`
}

type StorageFiles struct {
	Files []StorageFile `json:"files"`
}

type StorageFileUpload struct {
	Filename   string `json:"filename"`
	UploadURL  string `json:"uploadUrl"`
	UploadSize int64  `json:"uploadSize"`
}

type SDMountStatusResponse struct {
	Status string `json:"status"`
}

type SDStorageFileUpload struct {
	Filename   string `json:"filename"`
	UploadURL  string `json:"uploadUrl"`
	UploadSize int64  `json:"uploadSize"`
}

var imagesFolder = "/userdata/100ask_kvm/images"

func getMassStorageImage() (string, error) {
	return "", nil
}

func getMassStorageImages() ([]string, error) {
	return nil, nil
}

func setMassStorageMode(cdrom bool) error {
	return nil
}

func getMassStorageCDROMEnabled() (bool, error) {
	return false, nil
}

func initImagesFolder() error {
	return nil
}

func setInitialVirtualMediaState() error {
	return nil
}

func rpcMountBuiltInImage(filename string) error {
	return nil
}

func rpcCheckMountUrl(url string) (*VirtualMediaUrlInfo, error) {
	return nil, nil
}

func rpcMountWithStorage(filename string, mediaType VirtualMediaMode) error {
	return nil
}

func rpcMountWithHTTP(url string, mode VirtualMediaMode) error {
	return nil
}

func rpcMountWithWebRTC(filename string, size int64, mode VirtualMediaMode) error {
	return nil
}

func rpcMountWithSDStorage(filename string, mode VirtualMediaMode) error {
	return nil
}

func rpcUnmountImage() error {
	return nil
}

func rpcGetVirtualMediaState() (*VirtualMediaState, error) {
	return nil, nil
}

func rpcGetStorageSpace() (*StorageSpace, error) {
	return nil, nil
}

func rpcListStorageFiles() (*StorageFiles, error) {
	return nil, nil
}

func rpcDeleteStorageFile(filename string) error {
	return nil
}

func rpcStartStorageFileUpload(filename string, size int64) (*StorageFileUpload, error) {
	return nil, nil
}

func rpcGetSDStorageSpace() (*StorageSpace, error) {
	return nil, nil
}

func rpcResetSDStorage() error {
	return nil
}

func rpcMountSDStorage() error {
	return nil
}

func rpcUnmountSDStorage() error {
	return nil
}

func rpcListSDStorageFiles() (*StorageFiles, error) {
	return nil, nil
}

func rpcDeleteSDStorageFile(filename string) error {
	return nil
}

func rpcGetSDMountStatus() (*SDMountStatusResponse, error) {
	return &SDMountStatusResponse{Status: SDMountOK}, nil
}

func rpcStartSDStorageFileUpload(filename string, size int64) (*SDStorageFileUpload, error) {
	return nil, nil
}

func initVirtualMedia(r *gin.Engine) {}

func writeUmtprdConfFile(enable bool) error {
	return nil
}
