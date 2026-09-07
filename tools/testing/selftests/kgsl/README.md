# Polaris GPU accounting checks

Build from the ROM root with `m polaris_gpu_mem_test polaris_gpu_render_test`.
Use a root-capable diagnostic boot with SELinux Enforcing. These binaries are
test artifacts only and are not included in the ROM product packages.

`polaris_gpu_mem_test` reads commands from stdin and emits one JSON response per
command. Start two long-lived instances to verify process isolation. `alloc`,
`thread`, `legacy`, `ion` and `sparse` take a byte count, limited to 64 MiB per
request. `import` takes an ION slot, `free` a GPU slot; `invalid` checks zero-size
allocation rejection. `exit` intentionally leaves allocations for kernel
process-exit cleanup, while `quit` closes the device normally.

For a quiet system, compare `dumpsys gpu` and
`/sys/class/kgsl/kgsl/proc/<pid>/{kernel,user,ion}` after each command:

1. Allocate 4 MiB, allocate 2 MiB from a thread, allocate 1 MiB via the legacy
   ABI. Expect 4/6/7 MiB on the owning TGID, no separate TID entry, and matching
   global increments.
2. A second process allocates 8 MiB. The first process remains at 7 MiB.
3. Free all first-process objects: its map key disappears, not a retained zero.
4. Invalid and unsupported sparse allocation leave counters unchanged.
5. Allocate one 4 MiB ION buffer, import twice: process mappings become 8 MiB,
   global usage increases only 4 MiB. First unimport does not change the global
   count, final unimport removes 4 MiB.
6. Exit the second process with its allocation live: its PID disappears after
   deferred cleanup.

`polaris_gpu_render_test 600` renders on an EGL pbuffer for ten minutes. It
checks a pixel after every draw and reports renderer, frame counts and timing.
Capture `dumpsys gpu`, battery, thermal and kernel logs during/after the run;
verify its GPU process entry disappears after EGL teardown/process exit.
Reboot the same candidate and recheck the BPF program/map and GPU totals.

Counters are maintained before consumers attach. Each event carries an
absolute total rather than an allocation delta. A process with no subsequent
memory changes is first visible to a newly attached consumer on its next
accounting event; no periodic polling or synthetic zero values are generated.
