 # lithium

  a hobby x86-64 kernel. boots, runs a shell, forks processes. This project was made while i was researching and studying low level computer science and the Linux kernel.

  what's in here
  - 4-level paging with copy-on-write fork — fork actually copies pages lazily on write
  - slab allocator (7 size classes, 16–1024 bytes) + large alloc path
  - bitmap physical memory manager with refcounting
  - cooperative round-robin scheduler — tasks yield explicitly, no preemption yet
  - ELF64 loader — maps segments directly into user address spaces with NX per segment
  - 18 syscalls (read, write, open, close, fork, execve, exit, waitpid, mmap, brk, dup2, ...)
  - tmpfs — fully in-memory filesystem, no disk :(
  - /bin, /dev, /etc, /proc, /tmp all populated at boot
  - user-space shell with I/O redirection (< file, > file), external commands, builtins
  - some sexy ps/2 keyboard (wierd driver issue which makes inputs garbage sometimes), vga text mode, com1 serial driver.
  - acpi shutdown (parses real acpi tables, falls back to qemu magic ports)
  - some big issues with amd cpus but works on intel very well
  - hardware detection driver also works very well

  missing
  - preemption (no spinlocks)
  - disk driver / persistent filesystem (kinda raged while making so i quit)
  - smp
  - dynamic linker

  building-
  make
  make run

  requires: x86_64 cross-compiler, nasm, grub2, xorriso, qemu
  thank you everyone and i hope y'all make some cool projects from my source.
  No license but please credit me if y'all make some bangers from this

  idk how this thing boots tho, i made it very very very bad
