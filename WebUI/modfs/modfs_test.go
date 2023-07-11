package modfs_test

import (
	"errors"
	"io"
	"io/fs"
	"testing"
	"time"
	"webui/modfs"
)

type fakeFileInfo fakeFile

func (info fakeFileInfo) Name() string {
	return info.name
}

func (info fakeFileInfo) Size() int64 {
	return 0
}

func (info fakeFileInfo) Mode() fs.FileMode {
	return 0777
}

func (info fakeFileInfo) ModTime() time.Time {
	return info.modTime
}

func (info fakeFileInfo) IsDir() bool {
	return info.dirContent != nil
}

func (info fakeFileInfo) Sys() any {
	return nil
}

type fakeFile struct {
	name       string       // The name of the file or dir.
	modTime    time.Time    // ModTime
	dirContent *[]*fakeFile // Content of dir or nil if not dir.
}

func (f fakeFile) Stat() (fs.FileInfo, error) {
	return fakeFileInfo(f), nil
}

func (f fakeFile) Read(b []byte) (int, error) {
	return 0, io.EOF
}

func (f fakeFile) ReadDir(n int) ([]fs.DirEntry, error) {
	if f.dirContent == nil {
		// From doc /pkg/io/fs/#ReadDirFile
		// "It is permissible for any file to implement this interface,
		// but if so ReadDir should return an error for non-directories."
		return nil, errors.New("Not a dir")
	}
	if n >= 0 {
		// Only accept negative n on purpose.
		return nil, errors.New("invalid param, only negative n is acceptable")
	}

	var entries []fs.DirEntry
	for _, file := range *f.dirContent {
		entries = append(entries, (*fakeDirEntry)(file))
	}
	return entries, nil
}

func (f fakeFile) Close() error {
	return nil
}

type fakeDirEntry fakeFile

func (dir *fakeDirEntry) Name() string {
	return dir.name
}

func (dir *fakeDirEntry) IsDir() bool {
	return dir.dirContent != nil
}

func (dir *fakeDirEntry) Type() fs.FileMode {
	return 0777
}

func (dir *fakeDirEntry) Info() (fs.FileInfo, error) {
	return (*fakeFile)(dir).Stat()
}

type fakeFS struct{}

func (f fakeFS) Open(name string) (fs.File, error) {
	if !fs.ValidPath(name) {
		return nil, &fs.PathError{Err: fs.ErrInvalid}
	}

	switch name {
	case "file1":
		return &fakeFile{name: "file1", modTime: time.Unix(1, 2)}, nil
	case "dir1":
		return &fakeFile{name: "dir1", modTime: time.Unix(3, 4), dirContent: &[]*fakeFile{
			{name: "a", modTime: time.Unix(5, 6)},
		}}, nil
	default:
		return nil, &fs.PathError{Op: "open", Path: name, Err: fs.ErrNotExist}
	}
}

func Test_FS(t *testing.T) {
	var newTime = time.Unix(10, 20)
	var modfs = modfs.FS{FS: fakeFS{}, LastModified: newTime}
	if f, err := modfs.Open("file1"); err != nil {
		t.Fatal(err)
	} else if fi, err := f.Stat(); err != nil {
		t.Fatal(err)
	} else if modTime := fi.ModTime(); modTime != newTime {
		t.Fatalf("%v expected, but got %v", newTime, modTime)
	}
}

func Test_FS_ReadDir(t *testing.T) {
	var newTime = time.Unix(10, 11)
	var modfs = modfs.FS{FS: fakeFS{}, LastModified: newTime}
	if f, err := modfs.Open("dir1"); err != nil {
		t.Fatal(err)
	} else if entries, err := f.(fs.ReadDirFile).ReadDir(-1); err != nil {
		t.Fatal(err)
	} else if len(entries) != 1 {
		t.Fatal(entries)
	} else if info, err := entries[0].Info(); err != nil {
		t.Fatal(err)
	} else if modTime := info.ModTime(); modTime != newTime {
		t.Fatalf("%v expected, but got %v", newTime, modTime)
	}
}
