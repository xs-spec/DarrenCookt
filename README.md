# h2olog

[BPF](https://www.kernel.org/doc/html/latest/bpf/index.html) ([kernel doc](https://www.kernel.org/doc/Documentation/networking/filter.txt)) backed request logging client for the [H2O](https://github.com/h2o/h2o) server.

## Quickstart

Root privilege is required to interact with the BPF virtual machine.
The log line format is: `ConnID ReqID HeaderName HeaderValue`, except the first line that represents the HTTP protocol version.

```
$ sudo h2olog -p $(pgrep -o h2o)

888 1: HTTP/2.0
888 1: :authority torumk.com
888 1: :method GET
888 1: :path /
888 1: :scheme https
... and more ...
```

## Requirements

- H2O server built after [886db137](https://github.com/h2o/h2o/pull/2057/commits/886db13791bb03794901710974fb52cd1238968e)
- [BCC](https://iovisor.github.io/bcc/) (BPF Compiler Collection) [installed](https://github.com/iovisor/bcc/blob/master/INSTALL.md) on your system
- Root privilege to execute the program

## Program Anatomy

h2olog is a [BCC](https://github.com/iovisor/bcc) based single-file [Python](https://www.python.org/) program ([learn more](https://github.com/iovisor/bcc/blob/master/docs/reference_guide.md#bcc-python)).
This might change in the future (e.g. switch to [bpftrace](https://github.com/iovisor/bpftrace)), but the same CLI interface will be kept.

## TODO

- Option for output filtering
- Option to redirect the output to a file
