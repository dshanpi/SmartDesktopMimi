package usbgadget

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"time"

	"github.com/prometheus/procfs"
	"github.com/sourcegraph/tf-dag/dag"
)

// it's a minimalistic implementation of ansible's file module with some modifications
// to make it more suitable for our use case
// https://docs.ansible.com/ansible/latest/modules/file_module.html

// we use this to check if the files in the gadget config are in the expected state
// and to update them if they are not in the expected state

type FileState uint8
type ChangeState uint8
type FileChangeResolvedAction uint8

type ApplyFunc func(c *ChangeSet, changes []*FileChange) error

const (
	FileStateUnknown FileState = iota
	FileStateAbsent
	FileStateDirectory
	FileStateFile
	FileStateFileContentMatch
	FileStateFileWrite // update file content without checking
	FileStateMounted
	FileStateMountedConfigFS
	FileStateMountedFunctionFS
	FileStateSymlink
	FileStateSymlinkInOrderConfigFS // configfs is a shithole, so we need to check if the symlinks are created in the correct order
	FileStateSymlinkNotInOrderConfigFS
	FileStateTouch
)

var FileStateString = map[FileState]string{
	FileStateUnknown:                "UNKNOWN",
	FileStateAbsent:                 "ABSENT",
	FileStateDirectory:              "DIRECTORY",
	FileStateFile:                   "FILE",
	FileStateFileContentMatch:       "FILE_CONTENT_MATCH",
	FileStateFileWrite:              "FILE_WRITE",
	FileStateMounted:                "MOUNTED",
	FileStateMountedConfigFS:        "CONFIGFS_MOUNTED",
	FileStateSymlink:                "SYMLINK",
	FileStateSymlinkInOrderConfigFS: "SYMLINK_IN_ORDER_CONFIGFS",
	FileStateTouch:                  "TOUCH",
	FileStateMountedFunctionFS:      "FUNCTIONFS_MOUNTED",
}

const (
	ChangeStateUnknown ChangeState = iota
	ChangeStateRequired
	ChangeStateNotChanged
	ChangeStateChanged
	ChangeStateError
)

const (
	FileChangeResolvedActionUnknown FileChangeResolvedAction = iota
	FileChangeResolvedActionDoNothing
	FileChangeResolvedActionRemove
	FileChangeResolvedActionCreateFile
	FileChangeResolvedActionWriteFile
	FileChangeResolvedActionUpdateFile
	FileChangeResolvedActionAppendFile
	FileChangeResolvedActionCreateSymlink
	FileChangeResolvedActionRecreateSymlink
	FileChangeResolvedActionCreateDirectoryAndSymlinks
	FileChangeResolvedActionReorderSymlinks
	FileChangeResolvedActionCreateDirectory
	FileChangeResolvedActionRemoveDirectory
	FileChangeResolvedActionTouch
	FileChangeResolvedActionMountConfigFS
	FileChangeResolvedActionMountFunctionFS
)

var FileChangeResolvedActionString = map[FileChangeResolvedAction]string{
	FileChangeResolvedActionUnknown:                    "UNKNOWN",
	FileChangeResolvedActionDoNothing:                  "DO_NOTHING",
	FileChangeResolvedActionRemove:                     "REMOVE",
	FileChangeResolvedActionCreateFile:                 "FILE_CREATE",
	FileChangeResolvedActionWriteFile:                  "FILE_WRITE",
	FileChangeResolvedActionUpdateFile:                 "FILE_UPDATE",
	FileChangeResolvedActionAppendFile:                 "FILE_APPEND",
	FileChangeResolvedActionCreateSymlink:              "SYMLINK_CREATE",
	FileChangeResolvedActionRecreateSymlink:            "SYMLINK_RECREATE",
	FileChangeResolvedActionCreateDirectoryAndSymlinks: "DIR_CREATE_AND_SYMLINKS",
	FileChangeResolvedActionReorderSymlinks:            "SYMLINK_REORDER",
	FileChangeResolvedActionCreateDirectory:            "DIR_CREATE",
	FileChangeResolvedActionRemoveDirectory:            "DIR_REMOVE",
	FileChangeResolvedActionTouch:                      "TOUCH",
	FileChangeResolvedActionMountConfigFS:              "CONFIGFS_MOUNT",
	FileChangeResolvedActionMountFunctionFS:            "FUNCTIONFS_MOUNT",
}

type ChangeSet struct {
	Changes []FileChange
}

type RequestedFileChange struct {
	Component       string
	Key             string
	Path            string // will be used as Key if Key is empty
	ParamSymlinks   []symlink
	ExpectedState   FileState
	ExpectedContent []byte
	DependsOn       []string
	BeforeChange    []string // if the file is going to be changed, apply the change first
	Description     string
	IgnoreErrors    bool
	When            string // only apply the change if when meets the condition
}

type FileChange struct {
	RequestedFileChange
	ActualState   FileState
	ActualContent []byte
	resolvedDeps  []string
	checked       bool
	changed       ChangeState
	action        FileChangeResolvedAction
}

func (f *RequestedFileChange) String() string {
	var s string
	switch f.ExpectedState {
	case FileStateDirectory:
		s = fmt.Sprintf("dir: %s", f.Path)
	case FileStateFile:
		s = fmt.Sprintf("file: %s", f.Path)
	case FileStateSymlink:
		s = fmt.Sprintf("symlink: %s -> %s", f.Path, f.ExpectedContent)
	case FileStateSymlinkInOrderConfigFS:
		s = fmt.Sprintf("symlink_in_order_configfs: %s -> %s", f.Path, f.ExpectedContent)
	case FileStateSymlinkNotInOrderConfigFS:
		s = fmt.Sprintf("symlink_not_in_order_configfs: %s -> %s", f.Path, f.ExpectedContent)
	case FileStateAbsent:
		s = fmt.Sprintf("absent: %s", f.Path)
	case FileStateFileContentMatch:
		s = fmt.Sprintf("file: %s with content [%s]", f.Path, f.ExpectedContent)
	case FileStateFileWrite:
		s = fmt.Sprintf("write: %s with content [%s]", f.Path, f.ExpectedContent)
	case FileStateMountedConfigFS:
		s = fmt.Sprintf("configfs: %s", f.Path)
	case FileStateMountedFunctionFS:
		s = fmt.Sprintf("functionfs: %s", f.Path)
	case FileStateTouch:
		s = fmt.Sprintf("touch: %s", f.Path)
	case FileStateUnknown:
		s = fmt.Sprintf("unknown change for %s", f.Path)
	default:
		s = fmt.Sprintf("unknown expected state %d for %s", f.ExpectedState, f.Path)
	}

	if len(f.Description) > 0 {
		s += fmt.Sprintf(" (%s)", f.Description)
	}

	return s
}

func (f *RequestedFileChange) IsSame(other *RequestedFileChange) bool {
	return f.Path == other.Path &&
		f.ExpectedState == other.ExpectedState &&
		reflect.DeepEqual(f.ExpectedContent, other.ExpectedContent) &&
		reflect.DeepEqual(f.DependsOn, other.DependsOn) &&
		f.IgnoreErrors == other.IgnoreErrors
}

func (fc *FileChange) checkIfDirIsMountPoint() error {
	// check if the file is a mount point
	mounts, err := procfs.GetMounts()
	if err != nil {
		return fmt.Errorf("failed to get mounts")
	}

	for _, mount := range mounts {
		if mount.MountPoint == fc.Path {
			fc.ActualState = FileStateMounted
			fc.ActualContent = []byte(mount.Source)

			if mount.FSType == "configfs" {
				fc.ActualState = FileStateMountedConfigFS
			}

			return nil
		}
	}

	return nil
}

// GetActualState returns the actual state of the file at the given path.
func (fc *FileChange) getActualState() error {
	l := defaultLogger.With().Str("path", fc.Path).Logger()

	fi, err := os.Lstat(fc.Path)
	if err != nil {
		if os.IsNotExist(err) {
			fc.ActualState = FileStateAbsent
		} else {
			l.Warn().Err(err).Msg("failed to stat file")
			fc.ActualState = FileStateUnknown
		}
		return nil
	}

	// check if the file is a symlink
	if fi.Mode()&os.ModeSymlink == os.ModeSymlink {
		fc.ActualState = FileStateSymlink
		// get the target of the symlink
		target, err := os.Readlink(fc.Path)
		if err != nil {
			l.Warn().Err(err).Msg("failed to read symlink")
			return fmt.Errorf("failed to read symlink")
		}
		// check if the target is a relative path
		if !filepath.IsAbs(target) {
			// make it absolute
			target, err = filepath.Abs(filepath.Join(filepath.Dir(fc.Path), target))
			if err != nil {
				l.Warn().Err(err).Msg("failed to make symlink target absolute")
				return fmt.Errorf("failed to make symlink target absolute")
			}
		}
		fc.ActualContent = []byte(target)
		return nil
	}

	if fi.IsDir() {
		fc.ActualState = FileStateDirectory

		switch fc.ExpectedState {
		case FileStateMountedConfigFS:
			err := fc.checkIfDirIsMountPoint()
			if err != nil {
				l.Warn().Err(err).Msg("failed to check if dir is mount point")
				return err
			}
		case FileStateSymlinkInOrderConfigFS:
			state, err := checkIfSymlinksInOrder(fc, &l)
			if err != nil {
				l.Warn().Err(err).Msg("failed to check if symlinks are in order")
				return err
			}
			fc.ActualState = state
		}
		return nil
	}

	if fi.Mode()&os.ModeDevice == os.ModeDevice {
		l.Info().Msg("file is a device")
		return nil
	}

	// check if the file is a regular file
	if fi.Mode().IsRegular() {
		fc.ActualState = FileStateFile
		// get the content of the file
		content, err := os.ReadFile(fc.Path)
		if err != nil {
			l.Warn().Err(err).Msg("failed to read file")
			return fmt.Errorf("failed to read file")
		}
		fc.ActualContent = content
		return nil
	}

	l.Warn().Interface("file_info", fi.Mode()).Bool("is_dir", fi.IsDir()).Msg("unknown file type")

	return fmt.Errorf("unknown file type")
}

func (fc *FileChange) ResetActionResolution() {
	fc.checked = false
	fc.action = FileChangeResolvedActionUnknown
	fc.changed = ChangeStateUnknown
}

func (fc *FileChange) Action() FileChangeResolvedAction {
	if !fc.checked {
		fc.action = fc.getFileChangeResolvedAction()
		fc.checked = true
	}

	return fc.action
}

func (fc *FileChange) getFileChangeResolvedAction() FileChangeResolvedAction {
	l := defaultLogger.With().Str("path", fc.Path).Logger()

	// some actions are not needed to be checked
	switch fc.ExpectedState {
	case FileStateFileWrite:
		return FileChangeResolvedActionWriteFile
	case FileStateTouch:
		return FileChangeResolvedActionTouch
	case FileStateMountedFunctionFS:
		return FileChangeResolvedActionMountFunctionFS
	}

	// get the actual state of the file
	err := fc.getActualState()
	if err != nil {
		return FileChangeResolvedActionDoNothing
	}

	baseName := filepath.Base(fc.Path)

	switch fc.ExpectedState {
	case FileStateDirectory:
		// if the file is already a directory, do nothing
		if fc.ActualState == FileStateDirectory {
			return FileChangeResolvedActionDoNothing
		}
		return FileChangeResolvedActionCreateDirectory
	case FileStateFile:
		// if the file is already a file, do nothing
		if fc.ActualState == FileStateFile {
			return FileChangeResolvedActionDoNothing
		}
		return FileChangeResolvedActionCreateFile
	case FileStateFileContentMatch:
		// if the file is already a file with the expected content, do nothing
		if fc.ActualState == FileStateFile {
			looserMatch := baseName == "inquiry_string"
			if compareFileContent(fc.ActualContent, fc.ExpectedContent, looserMatch) {
				return FileChangeResolvedActionDoNothing
			}
			// TODO: move this to somewhere else
			// this is a workaround for the fact that the file is not updated if it has no content
			if baseName == "file" &&
				bytes.Equal(fc.ActualContent, []byte{}) &&
				bytes.Equal(fc.ExpectedContent, []byte{0x0a}) {
				return FileChangeResolvedActionDoNothing
			}
			return FileChangeResolvedActionUpdateFile
		}
		return FileChangeResolvedActionCreateFile
	case FileStateSymlink:
		// if the file is already a symlink, check if the target is the same
		if fc.ActualState == FileStateSymlink {
			if reflect.DeepEqual(fc.ActualContent, fc.ExpectedContent) {
				return FileChangeResolvedActionDoNothing
			}
			return FileChangeResolvedActionRecreateSymlink
		}
		return FileChangeResolvedActionCreateSymlink
	case FileStateSymlinkInOrderConfigFS:
		// if the file is already a symlink, check if the target is the same
		if fc.ActualState == FileStateSymlinkInOrderConfigFS {
			return FileChangeResolvedActionDoNothing
		}
		return FileChangeResolvedActionReorderSymlinks
	case FileStateAbsent:
		if fc.ActualState == FileStateAbsent {
			return FileChangeResolvedActionDoNothing
		}
		return FileChangeResolvedActionRemove
	case FileStateMountedConfigFS:
		if fc.ActualState == FileStateMountedConfigFS {
			return FileChangeResolvedActionDoNothing
		}
		return FileChangeResolvedActionMountConfigFS
	default:
		l.Warn().Interface("file_change", FileStateString[fc.ExpectedState]).Msg("unknown expected state")
		return FileChangeResolvedActionDoNothing
	}
}

func (c *ChangeSet) AddFileChangeStruct(r RequestedFileChange) {
	fc := FileChange{
		RequestedFileChange: r,
	}
	c.Changes = append(c.Changes, fc)
}

func (c *ChangeSet) AddFileChange(component string, path string, expectedState FileState, expectedContent []byte, dependsOn []string, description string) {
	c.AddFileChangeStruct(RequestedFileChange{
		Component:       component,
		Path:            path,
		ExpectedState:   expectedState,
		ExpectedContent: expectedContent,
		DependsOn:       dependsOn,
		Description:     description,
	})
}

func (c *ChangeSet) ApplyChanges() error {
	r := ChangeSetResolver{
		changeset: c,
		g:         &dag.AcyclicGraph{},
		l:         defaultLogger,
	}

	return r.Apply()
}

func (c *ChangeSet) applyChange(change *FileChange) error {
	// 记录操作详情
	contentPreview := ""
	if len(change.ExpectedContent) > 0 && len(change.ExpectedContent) <= 64 {
		contentPreview = string(change.ExpectedContent)
	} else if len(change.ExpectedContent) > 64 {
		contentPreview = string(change.ExpectedContent[:64]) + "..."
	}

	defaultLogger.Debug().
		Str("operation", FileChangeResolvedActionString[change.Action()]).
		Str("path", change.Path).
		Str("content_preview", contentPreview).
		Int("content_length", len(change.ExpectedContent)).
		Msg("executing file operation")

	switch change.Action() {
	case FileChangeResolvedActionWriteFile:
		defaultLogger.Debug().Str("path", change.Path).Msg("writing file")
		return os.WriteFile(change.Path, change.ExpectedContent, 0644)
	case FileChangeResolvedActionUpdateFile:
		defaultLogger.Debug().Str("path", change.Path).Msg("updating file")
		err := os.WriteFile(change.Path, change.ExpectedContent, 0644)
		if err != nil && strings.Contains(err.Error(), "device or resource busy") {
			defaultLogger.Error().
				Str("path", change.Path).
				Str("content", contentPreview).
				Msg("device or resource busy - gadget may be bound to UDC")
			return fmt.Errorf("%w (hint: gadget may be bound to UDC, try unbinding first)", err)
		}
		return err
	case FileChangeResolvedActionCreateFile:
		defaultLogger.Debug().Str("path", change.Path).Msg("creating file")
		return os.WriteFile(change.Path, change.ExpectedContent, 0644)
	case FileChangeResolvedActionCreateSymlink:
		target := string(change.ExpectedContent)
		defaultLogger.Debug().
			Str("path", change.Path).
			Str("target", target).
			Msg("creating symlink")
		return os.Symlink(target, change.Path)
	case FileChangeResolvedActionRecreateSymlink:
		target := string(change.ExpectedContent)
		defaultLogger.Debug().
			Str("path", change.Path).
			Str("target", target).
			Msg("recreating symlink")
		if err := os.Remove(change.Path); err != nil {
			return fmt.Errorf("failed to remove symlink: %w", err)
		}
		return os.Symlink(target, change.Path)
	case FileChangeResolvedActionReorderSymlinks:
		defaultLogger.Debug().
			Str("path", change.Path).
			Int("symlink_count", len(change.ParamSymlinks)).
			Msg("reordering symlinks")
		return recreateSymlinks(change, nil)
	case FileChangeResolvedActionCreateDirectory:
		defaultLogger.Debug().Str("path", change.Path).Msg("creating directory")
		return os.MkdirAll(change.Path, 0755)
	case FileChangeResolvedActionRemove:
		defaultLogger.Debug().Str("path", change.Path).Msg("removing file")
		return os.Remove(change.Path)
	case FileChangeResolvedActionRemoveDirectory:
		defaultLogger.Debug().Str("path", change.Path).Msg("removing directory")
		return os.RemoveAll(change.Path)
	case FileChangeResolvedActionTouch:
		defaultLogger.Debug().Str("path", change.Path).Msg("touching file")
		return os.Chtimes(change.Path, time.Now(), time.Now())
	case FileChangeResolvedActionMountConfigFS:
		defaultLogger.Debug().Str("path", change.Path).Msg("mounting configfs")
		return mountConfigFS(change.Path)
	case FileChangeResolvedActionMountFunctionFS:
		defaultLogger.Debug().Str("path", change.Path).Msg("mounting functionfs")
		return mountFunctionFS(change.Path)
	case FileChangeResolvedActionDoNothing:
		return nil
	default:
		return fmt.Errorf("unknown action: %d", change.Action())
	}
}

func (c *ChangeSet) Apply() error {
	return c.ApplyChanges()
}
