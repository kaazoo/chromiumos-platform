// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VIRTUAL_MACHINE_H_
#define VM_TOOLS_CONCIERGE_VIRTUAL_MACHINE_H_

#include <stdint.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/macros.h>
#include <base/time/time.h>
#include <brillo/process.h>

#include "vm_tools/concierge/mac_address_generator.h"
#include "vm_tools/concierge/subnet_pool.h"
#include "vm_tools/concierge/vsock_cid_pool.h"

#include "container_guest.grpc.pb.h"  // NOLINT(build/include)
#include "vm_guest.grpc.pb.h"  // NOLINT(build/include)

namespace vm_tools {
namespace concierge {

// Represents a single instance of a running virtual machine.
class VirtualMachine {
 public:
  // Describes how maitre'd should handle exits for processes launched via the
  // StartProcess() function.
  enum class ProcessExitBehavior {
    // Don't respawn the process when it exits.
    ONE_SHOT,
    // Respawn the process when it exits.
    RESPAWN_ON_EXIT,
  };

  // Describes the status of a process launched inside the VM.
  enum class ProcessStatus {
    // ¯\_(ツ)_/¯
    UNKNOWN,

    // The process exited normally.
    EXITED,

    // The process was killed by a signal.
    SIGNALED,

    // Successfully launched but may or may not have exited yet.
    LAUNCHED,

    // One or more setup steps failed and the process did not launch.
    FAILED,
  };

  // Type of a disk image.
  enum class DiskImageType {
    // Raw disk image file.
    RAW,

    // QCOW2 disk image.
    QCOW2,
  };

  // Describes a disk image to be mounted inside the VM.
  struct Disk {
    // Path to the disk image on the host.
    base::FilePath path;

    // Whether the disk should be writable by the VM.
    bool writable;

    // Type of the disk image.
    DiskImageType image_type;
  };

  // Linux application ID and its icon content.
  struct Icon {
    std::string desktop_file_id;
    std::string content;
  };

  // Starts a new virtual machine.  Returns nullptr if the virtual machine
  // failed to start for any reason.
  static std::unique_ptr<VirtualMachine> Create(
      base::FilePath kernel,
      base::FilePath rootfs,
      std::vector<Disk> disks,
      MacAddress mac_addr,
      std::unique_ptr<SubnetPool::Subnet> subnet,
      uint32_t vsock_cid,
      base::FilePath runtime_dir);
  ~VirtualMachine();

  // Shuts down the VM.  First attempts a clean shutdown of the VM by sending
  // a Shutdown RPC to maitre'd.  If that fails, attempts to shut down the VM
  // using the control socket for the hypervisor.  If that fails, then sends a
  // SIGTERM to the hypervisor.  Finally, if nothing works forcibly stops the VM
  // by sending it a SIGKILL.  Returns true if the VM was shut down and false
  // otherwise.
  bool Shutdown();

  // Launches a process inside the VM and returns without waiting for it to
  // exit. |args[0]| must be either the name of a program in the default PATH
  // inside the VM or the path to the program to be executed.
  bool StartProcess(std::vector<std::string> args,
                    std::map<std::string, std::string> env,
                    ProcessExitBehavior exit_behavior);

  // Launches a process inside the VM and synchronously waits for it to exit.
  // |args[0]| must be either the name of a program in the default PATH
  // inside the VM or the path to the program to be executed.
  bool RunProcess(std::vector<std::string> args,
                  std::map<std::string, std::string> env);

  // Like RunProcess, but overrides the default timeout for a process to exit
  // with |timeout_seconds|.
  bool RunProcessWithTimeout(std::vector<std::string> args,
                             std::map<std::string, std::string> env,
                             base::TimeDelta timeout);

  // Configures the network interfaces inside the VM.  Returns true if
  // successful, false otherwise.
  bool ConfigureNetwork();

  // Mounts a file system inside the VM.  Both |source| (if it is a file path)
  // and |target| must be valid paths inside the VM.  Returns true on success.
  bool Mount(std::string source,
             std::string target,
             std::string fstype,
             uint64_t mountflags,
             std::string options);

  // Sets the container subnet for this VM to |subnet|. This subnet is intended
  // to be provided to a container runtime as a DHCP pool.
  void SetContainerSubnet(std::unique_ptr<SubnetPool::Subnet> subnet);

  // The pid of the child process.
  pid_t pid() { return process_.pid(); }

  // The VM's cid.
  uint32_t cid() const { return vsock_cid_; }

  // The IPv4 address of the VM's gateway in network byte order.
  uint32_t GatewayAddress() const;

  // The IPv4 address of the VM in network byte order.
  uint32_t IPv4Address() const;

  // The netmask of the VM's subnet in network byte order.
  uint32_t Netmask() const;

  // The VM's container subnet netmask in network byte order. Returns INADDR_ANY
  // if there is no container subnet.
  uint32_t ContainerNetmask() const;

  // The VM's container subnet prefix. Returns 0 if there is no container
  // subnet.
  size_t ContainerPrefix() const;

  // The first address in the VM's container subnet in network byte order.
  // Returns INADDR_ANY if there is no container subnet.
  uint32_t ContainerSubnet() const;

  // Register the IP address for a container token within this VM. Returns true
  // if the token is valid, false otherwise.
  bool RegisterContainerIp(const std::string& container_token,
                           const std::string& container_ip);

  // Unregister the IP address for a container token within this VM. Returns
  // true if the token is valid, false otherwise.
  bool UnregisterContainerIp(const std::string& container_token);

  // Generates a random token string that should be passed into the container
  // which can then be used by the container to identify itself when it
  // communicates back with us.
  std::string GenerateContainerToken(const std::string& container_name);

  // Returns the name of the container associated with the passed in
  // |container_token|. Returns the empty string if no such mapping exists. This
  // will only return a name that has been confirmed after calling
  // RegisterContainerIp.
  std::string GetContainerNameForToken(const std::string& container_token);

  // Launches the application associated with |desktop_file_id| in the container
  // named |container_name| within this VM. Returns true on success, false
  // otherwise and fills out |out_error| on failure.
  bool LaunchContainerApplication(const std::string& container_name,
                                  const std::string& desktop_file_id,
                                  std::string* out_error);

  // Returns whether there is a connected stub to Garcon running inside the
  // named |container_name| within this VM.
  bool IsContainerRunning(const std::string& container_name);

  // Gets icons of those applications with their desktop file IDs specified
  // by |desktop_file_ids| from the container named |container_name| within
  // this VM. The icons should have size of |icon_size| and designed scale of
  // |scale|. The icons are returned through the paramenter |icons|.
  bool GetContainerAppIcon(const std::string& container_name,
                           std::vector<std::string> desktop_file_ids,
                           uint32_t icon_size,
                           uint32_t scale,
                           std::vector<Icon>* icons);

  // Gets a list of all the active container names in this VM.
  std::vector<std::string> GetContainerNames();

  static std::unique_ptr<VirtualMachine> CreateForTesting(
      MacAddress mac_addr,
      std::unique_ptr<SubnetPool::Subnet> subnet,
      uint32_t vsock_cid,
      base::FilePath runtime_dir,
      std::unique_ptr<vm_tools::Maitred::Stub> stub);

 private:
  VirtualMachine(MacAddress mac_addr,
                 std::unique_ptr<SubnetPool::Subnet> subnet,
                 uint32_t vsock_cid,
                 base::FilePath runtime_dir);

  // Starts the VM with the given kernel and root file system.
  bool Start(base::FilePath kernel,
             base::FilePath rootfs,
             std::vector<Disk> disks);

  // Launches a process inside the VM.  Shared implementation of both
  // StartProcess and RunProcess.
  bool LaunchProcess(std::vector<std::string> args,
                     std::map<std::string, std::string> env,
                     bool respawn,
                     bool wait_for_exit,
                     int64_t timeout_seconds);

  void set_stub_for_testing(std::unique_ptr<vm_tools::Maitred::Stub> stub);

  // EUI-48 mac address for the VM's network interface.
  MacAddress mac_addr_;

  // The /30 subnet assigned to the VM.
  std::unique_ptr<SubnetPool::Subnet> subnet_;

  // An optional /28 container subnet.
  std::unique_ptr<SubnetPool::Subnet> container_subnet_;

  // Virtual socket context id to be used when communicating with this VM.
  uint32_t vsock_cid_;

  // Mapping of container tokens to names. The tokens are used to securely
  // identify a container when it connects back to concierge to identify itself.
  std::map<std::string, std::string> container_token_to_name_;

  // Pending map of container tokens to names. The tokens are put in here when
  // they are generated and removed once we have a connection from the
  // container. We do not immediately put them in the contaienr_token_to_name_
  // map because we may get redundant requests to start a container that is
  // already running and we don't want to invalidate an in-use token.
  std::map<std::string, std::string> pending_container_token_to_name_;

  // Mapping of container names to a stub for making RPC requests to the garcon
  // process inside the container.
  std::map<std::string, std::unique_ptr<vm_tools::container::Garcon::Stub>>
      container_name_to_garcon_stub_;

  // Mapping of container names to a grpc Channel to the garcon process inside
  // the container, which we can test for connectedness.
  std::map<std::string, std::shared_ptr<grpc::Channel>>
      container_name_to_garcon_channel_;

  // Runtime directory for this VM.
  base::ScopedTempDir runtime_dir_;

  // Handle to the VM process.
  brillo::ProcessImpl process_;

  // Stub for making RPC requests to the maitre'd process inside the VM.
  std::unique_ptr<vm_tools::Maitred::Stub> stub_;

  DISALLOW_COPY_AND_ASSIGN(VirtualMachine);
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_VIRTUAL_MACHINE_H_
