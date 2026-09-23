# IPKVM GPL-2.0 boundary

The IPKVM implementation currently lives in `apps/ipkvm/upstream` and keeps
its GPL-2.0 license. It is built and packaged as a separate executable and may
communicate with product services only through documented Unix sockets. It
must not be statically linked into the differently licensed product core.
