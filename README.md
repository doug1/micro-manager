
micro-manager
=============

This is a user-mode process manager for Amazon EC2 t1.micro instances
that tries to make micro instances a bit more usable and and even out the
throttling and latency.  It does this by pausing and un-pausing processes
many times a second, preventing processes from using all available CPU,
which reduces the chance and throttling and CPU time stealing.

It must run as root on a single-process Linux system and only tries to
manage the CPU use of non-root processes, so make sure that as little
as possible is running as root.  The usual root-owned problem processes
are cron jobs, updatedb, makewhatis, etc.

To test, run micro-manager as root, and a CPU-intense process as non-root
and top (as root) in another window.  It may take a minute or so for things
to settle down after you start micro-manager, since EC2 seems to adjust
throttling every 30-60 seconds.

