# Mini Linux Sandbox — Learning Notes

`minibox` is a small educational Linux sandbox written in C.

The goal is not to build a production container runtime.
The goal is to understand the Linux kernel mechanisms behind process isolation and resource control.

Build:

```bash
make
```

## Core Idea

A sandboxed process is still a normal Linux process.

It becomes isolated because the kernel gives it different views, identities, filesystem roots, and resource limits.

```text
process
├── namespaces  -> control what it can see
├── cgroups     -> control what it can use
├── rootfs      -> control what it sees as /
└── uid/gid map -> control what users mean inside the sandbox
```

Main mental model:

```text
Namespaces -> visibility isolation
Cgroups    -> resource control
Rootfs     -> filesystem view
Userns     -> user/group identity mapping
```

## What We Built

We built a small runtime that starts a command inside an isolated environment.

The runtime prepares the environment first, then executes the target command.

```text
parent process
│
├── parse CLI options
├── create sync pipe
├── allocate child stack
├── clone child with namespace flags
├── configure user namespace
├── configure cgroup limits
├── move child into cgroup
├── wake child
├── wait for child
└── cleanup cgroup

child process
│
├── starts inside new namespaces
├── waits for parent setup
├── becomes UID 0/GID 0 inside user namespace
├── sets hostname
├── makes mounts private
├── enters rootfs
├── mounts /proc
└── execs target command
```

## `clone()`

`clone()` is the main syscall used to create the sandboxed child process.

Unlike `fork()`, `clone()` can create a process with new namespaces.

We used:

```c
CLONE_NEWUTS
CLONE_NEWPID
CLONE_NEWNS
CLONE_NEWUSER
CLONE_NEWNET
SIGCHLD
```

Meaning:

```text
CLONE_NEWUTS   -> new hostname namespace
CLONE_NEWPID   -> new PID namespace
CLONE_NEWNS    -> new mount namespace
CLONE_NEWUSER  -> new user namespace
CLONE_NEWNET   -> new network namespace
SIGCHLD        -> parent can wait for the child
```

## Child Stack

`clone()` requires a stack for the child process.

```c
stack = malloc(STACK_SIZE);
stack_top = stack + STACK_SIZE;
```

The stack usually grows downward, so we pass the end of the memory block to `clone()`.

```text
low memory                         high memory
|---------------------------------------------|
^                                             ^
stack                                    stack_top
```

## UTS Namespace

The UTS namespace isolates hostname and domain name.

Inside the sandbox, the process can change the hostname without changing the host machine hostname.

```text
host hostname:    Inception
sandbox hostname: box1
```

This proves hostname visibility is namespace-specific.

## PID Namespace

The PID namespace isolates process IDs.

The first process inside a new PID namespace becomes PID `1`.

The same process can have different PIDs depending on where it is viewed from:

```text
host view:    PID 4214
sandbox view: PID 1
```

This is why the parent must use the host PID when writing to cgroup files.

## Mount Namespace

The mount namespace gives the process a separate mount table.

That means mount and unmount operations can be private to the sandbox.

We also make mount propagation private:

```c
mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
```

This means mount changes inside the sandbox should not propagate to the host.

Important mount propagation modes:

```text
shared   -> mount changes can propagate to other namespaces
private  -> mount changes stay only in this namespace
slave    -> receives changes from another namespace but does not send them back
```

## `/proc`

`/proc` is a virtual filesystem that exposes process and kernel information.

Tools like `ps` read from `/proc`.

After creating a new PID namespace, we mount a fresh `/proc`:

```c
mount("proc", "/proc", "proc", 0, NULL);
```

This makes `/proc` match the sandbox PID namespace.

Without this step, tools like `ps` may still show host process information.

## User Namespace

The user namespace isolates user IDs and group IDs.

This allows the process to be `root` inside the sandbox without necessarily being real root on the host.

Example mapping:

```text
0 1000 1
```

Meaning:

```text
inside UID 0 maps to host UID 1000
```

So inside the sandbox:

```text
uid=0(root)
```

But on the host, that process maps to a normal user.

This is why `root` inside a user namespace is not the same as unrestricted host root.

## UID/GID Mapping

User namespace mappings are configured through files in `/proc/<pid>/`:

```text
/proc/<pid>/uid_map
/proc/<pid>/gid_map
/proc/<pid>/setgroups
```

The parent writes these files before the child continues.

The child waits because it cannot safely continue before its UID/GID mapping exists.

## `setuid(0)` and `setgid(0)`

After the parent writes the UID/GID maps, the child calls:

```c
setgid(0);
setuid(0);
```

This switches the child to UID `0` and GID `0` inside the user namespace.

Order matters:

```text
setgid(0) first
setuid(0) second
```

After changing UID, changing GID may fail depending on privileges.

## Network Namespace

The network namespace isolates network interfaces, routes, IP addresses, firewall rules, and ports.

With `CLONE_NEWNET`, the sandbox gets its own network stack.

At first, this usually means the sandbox has no internet access.

That is expected.

Network isolation alone does not configure networking. To give the sandbox internet later, we would need:

```text
veth pair
IP addresses
routes
host-side NAT
```

## Root Filesystem

A root filesystem, or `rootfs`, is the filesystem tree that a process sees as `/`.

Without a separate rootfs, the sandbox still sees the host filesystem.

Before rootfs isolation:

```text
sandbox / -> host /
```

After rootfs isolation:

```text
sandbox / -> ./rootfs
```

This is important because namespaces alone do not automatically hide host files.

## `chroot()`

`chroot()` changes the root directory of the current process.

Concept:

```text
chroot("./rootfs")
```

After this:

```text
/ inside the sandbox means ./rootfs on the host
```

This gives basic filesystem isolation.

It is good for learning, but it is weaker than `pivot_root()`.

## Cgroups

Cgroups control resources used by processes.

Namespaces answer:

```text
What can the process see?
```

Cgroups answer:

```text
How much can the process use?
```

We used cgroup v2 through files under:

```text
/sys/fs/cgroup
```

The project creates a cgroup and writes values to control files.

## `memory.max`

`memory.max` limits memory usage for the cgroup.

Example:

```text
20971520
```

means:

```text
20 MB
```

The CLI can accept values like:

```text
20M
512K
1G
```

But cgroup v2 expects bytes, so the project converts them before writing to `memory.max`.

## `memory.swap.max`

`memory.swap.max` controls swap usage for the cgroup.

We set it to:

```text
0
```

That disables swap for the sandbox.

This makes memory-limit behavior easier to observe.

## `pids.max`

`pids.max` limits the number of processes or threads inside the cgroup.

Example:

```text
pids.max = 5
```

The whole cgroup can contain only five processes or threads.

This protects the system from fork bombs or uncontrolled process creation.

## `cpu.max`

`cpu.max` controls CPU usage.

Format:

```text
quota period
```

Example:

```text
50000 100000
```

Meaning:

```text
The cgroup can use 50ms of CPU every 100ms.
```

That is about 50% of one CPU core.

## `cgroup.procs`

To move a process into a cgroup, the parent writes the child PID into:

```text
/sys/fs/cgroup/<group>/cgroup.procs
```

Important: this must be the child PID from the host PID namespace.

```text
host sees child as:    4214
sandbox sees child as: 1
```

The parent writes `4214`, not `1`.

## Parent-Child Synchronization

The child must not start running the final command before the parent finishes setup.

So we use a pipe.

```text
parent creates pipe
parent clones child
child waits on pipe
parent writes UID/GID maps
parent configures cgroup
parent moves child into cgroup
parent writes one byte to pipe
child continues
```

This prevents race conditions.

Without this, the child might execute before limits or mappings are ready.

## `execvp()`

The child process does setup first.

Then it calls:

```c
execvp(config->command[0], config->command);
```

`execvp()` replaces the current child process with the target command.

Important: `execvp()` does not create a new process.

It keeps the same process, namespaces, and cgroup membership.

```text
child setup process
        |
        | execvp()
        v
target command
```

## What We Learned

The project shows that a sandbox is built by combining several kernel features.

```text
clone()       -> create process with namespace flags
namespaces    -> isolate system views
cgroups       -> limit resource usage
rootfs        -> provide a different /
chroot()      -> enter the rootfs
uid_map       -> define user identity mapping
gid_map       -> define group identity mapping
pipe          -> synchronize parent and child
execvp()      -> run the final command
```

The key lesson:\
 **A sandboxed process is still a normal Linux process.\
The kernel just gives it different views, identities, filesystems, and limits.**

## Final Mental Model

```text
Namespaces:
  What can the process see?

Cgroups:
  How much can the process use?

Rootfs:
  What does / mean?

User namespace:
  What does root mean?

Network namespace:
  What network stack does it use?

Seccomp:
  Which syscalls can it call?
```