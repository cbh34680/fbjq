# fbjq

**Fuse-based Job Queue**

fbjq is a lightweight job queue system for Linux.

It provides job queues as a FUSE filesystem, allowing jobs to be submitted through a simple file-based interface.

Multiple queues can be managed independently, with configurable execution users and concurrency limits. Job execution is handled by systemd.

fbjq aims to provide a simple job queue interface without requiring a dedicated client API or protocol.
