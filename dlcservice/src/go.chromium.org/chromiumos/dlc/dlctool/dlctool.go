// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The dlctool executable allows for modification of DLCs directly on the device.
package main

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path"

	"go.chromium.org/chromiumos/dlc/dlclib"
	"go.chromium.org/chromiumos/dlc/dlctool/parse"
)

const (
	unsquashfsPath = "/usr/bin/unsquashfs"
)

func dlctoolShell(args []string) {
	cmd := &exec.Cmd{
		Path:   dlclib.ToolShellPath,
		Args:   append([]string{dlclib.ToolShellPath}, args...),
		Stdout: os.Stdout,
		Stderr: os.Stderr,
		Env:    os.Environ(),
	}
	if err := cmd.Run(); err != nil {
		log.Fatalf("%v failed: %v", os.Args[0], err)
	}
}

func isDlcInstalled(id *string) bool {
	out, err := dlclib.Util.Read(id)
	if err != nil {
		log.Fatalf("Failed to read state: %v", err)
	}

	state := struct {
		State int `json:"state"`
	}{}
	err = json.Unmarshal(out, &state)

	if err != nil {
		log.Fatalf("Failed to unmarshal DLC (%s) state", *id)
	}

	return state.State == 2
}

func isDlcPreloadable(id *string) bool {
	_, err := os.Stat(path.Join(dlclib.PreloadPath, *id, "package", dlclib.ImageFile))
	return !os.IsNotExist(err)
}

func isDlcScaled(id *string) bool {
	out, err := dlclib.MetadataUtil.Read(id)
	if err != nil {
		log.Fatalf("Failed to read metadata: %v", err)
	}

	metadata := struct {
		Manifest struct {
			Scaled bool `json:"scaled"`
		} `json:"manifest"`
	}{}
	err = json.Unmarshal(out, &metadata)

	if err != nil {
		log.Fatalf("Failed to unmarshal DLC (%s)", *id)
	}

	return metadata.Manifest.Scaled
}

func isDlcForceOTA(id *string) bool {
	out, err := dlclib.MetadataUtil.Read(id)
	if err != nil {
		log.Fatalf("Failed to read metadata: %v", err)
	}

	metadata := struct {
		Manifest struct {
			ForceOTA bool `json:"force-ota"`
		} `json:"manifest"`
	}{}
	err = json.Unmarshal(out, &metadata)

	if err != nil {
		log.Fatalf("Failed to unmarshal DLC (%s)", *id)
	}

	return metadata.Manifest.ForceOTA
}

func getDlcImagePath(id *string) string {
	out, err := dlclib.Util.Read(id)
	if err != nil {
		log.Fatalf("dlcImagePath: Failed to read state: %v", err)
	}

	state := struct {
		ImagePath string `json:"image_path"`
	}{}
	err = json.Unmarshal(out, &state)

	if err != nil {
		log.Fatalf("dlcImagePath: Failed to unmarshal DLC (%s)", *id)
	}

	return state.ImagePath
}

func installDlc(id *string) error {
	cmd := &exec.Cmd{
		Path: dlclib.UtilPath,
		Args: append([]string{dlclib.UtilPath}, "--install", "--id="+*id),
	}
	return cmd.Run()
}

func tryInstallingDlc(id *string) error {
	if isDlcInstalled(id) {
		log.Printf("DLC (%s) is already installed, continuing...\n", *id)
		return nil
	}

	if isDlcPreloadable(id) {
		log.Printf("Trying to install DLC (%s) because it's preloaded.\n", *id)
	} else if isDlcScaled(id) {
		log.Printf("Trying to install DLC (%s) because it's scaled.\n", *id)
	} else if isDlcForceOTA(id) {
		log.Printf("Trying to install DLC (%s) because it's force-ota.\n", *id)
	} else {
		return fmt.Errorf("tryInstallingDlc failed: Can't install DLC (%s)", *id)
	}

	if err := installDlc(id); err != nil {
		return fmt.Errorf("tryInstallingDlc failed: %w", err)
	}
	log.Printf("Installed DLC (%s)\n", *id)
	return nil
}

func extractDlc(id, path *string) error {
	// TODO(b/335722339): Add support for other filesystems based on image type.
	cmd := &exec.Cmd{
		Path: unsquashfsPath,
		Args: []string{unsquashfsPath, "-d", *path, getDlcImagePath(id)},
	}

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("extractDlc: failed to decompress: %w", err)
	}

	return nil
}

func unpackDlc(id, path *string) error {
	if _, err := os.Stat(*path); !os.IsNotExist(err) {
		return fmt.Errorf("%s is a path which already exists", *path)
	}

	if err := tryInstallingDlc(id); err != nil {
		return fmt.Errorf("unpackDlc: failed installing DLC: %w", err)
	}

	if err := extractDlc(id, path); err != nil {
		return fmt.Errorf("unpackDlc: failed extracting: %w", err)
	}

	return nil
}

func main() {
	dlclib.Init()
	p, err := parse.Args(os.Args[0], os.Args[1:])
	if err != nil {
		log.Fatalf("Parsing flags failed: %v", err)
	}

	if *parse.FlagUnpack {
		log.Printf("Unpacking DLC (%s) to: %s\n", *parse.FlagID, p)
		if err := unpackDlc(parse.FlagID, &p); err != nil {
			log.Fatalf("Unpacking DLC (%s) failed: %v", *parse.FlagID, err)
		}
		return
	}

	if *parse.FlagShell {
		dlctoolShell(os.Args[1:])
		return
	}

	log.Fatal("Please use shell variant.")
}
