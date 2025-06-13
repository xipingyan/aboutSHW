

# kinesthetic learning

[Making use of POSIX Capabilities in Linux to Reduce the Need to root Permissions](https://www.youtube.com/watch?v=xLUaHhmZWIE)

the content is little outdated: Newer kernels (since around Linux kernel 4.2): ping can function without cap_net_raw thanks to kernel changes allowing ICMP sockets without raw access.

Linux distributions didn't set caps of command files like passwd/renice/..., as pointed out [here](https://stackoverflow.com/questions/60821695/why-many-linux-distros-use-setuid-instead-of-capabilities), so we need to do it by ourselves using `setcap`/`getcap` to see how it works.

we can change it to renice which calls [setpriority](https://man7.org/linux/man-pages/man3/setpriority.3p.html) and requires `cap_sys_nice` capabilities.

```bash
$ getcap /bin/renice
# nothing
$ renice -n 0 -u 2
renice: failed to set priority for 2 (user ID): Operation not permitted
```
we can invoke using `sudo`, but if we want to allow normal user to use this tools, we can:
 - use SUID: A file with SUID always executes as the user who owns the file, regardless of the user passing the command
```bash
$ sudo chmod u+s /bin/renice
$ ll /bin/renice 
-rwsr-xr-x 1 root root 16920 Apr  3  2023 /bin/renice
# notice ths `s` bit is set, now normal user can use renice too
$ renice -n 0 -u 2
2 (user ID) old priority 0, new priority 0
```
 - set cap_sys_nice capability to the bin file, this is not done by Linux distribution by default, system admin can do it by hand. this is safer then SUID since only one specific capability is given to the program, it can do smaller damange if being compromised.

```bash
$ sudo setcap cap_sys_nice=eip /bin/renice
$ getcap /bin/renice
/bin/renice cap_sys_nice=eip
# now it works
$ renice -n 0 -u 2
2 (user ID) old priority 0, new priority 0
# remove capabilities after experiment
$ sudo setcap -r /bin/renice
# only set permitted capability is not enough, program need to call capset to make it effective
$ sudo setcap cap_sys_nice+p ./a.out 
$ getcap ./a.out 
./a.out cap_sys_nice=p

```

> https://www.man7.org/linux/man-pages/man7/capabilities.7.html

## inspecting

> https://www.youtube.com/watch?v=WYC6DHzWzFQ
> [3:07] what is effective/permitted/bound/inherit

we can use `pidof` & `cat /proc/<pid>/status` or `getpcaps <pid>` to inspect how capability is taking effect in system:

```bash
# process 1 has all capabilities
$ cat /proc/1/status | grep Cap
CapInh: 0000000000000000
CapPrm: 000001ffffffffff
CapEff: 000001ffffffffff
CapBnd: 000001ffffffffff
CapAmb: 0000000000000000
$ getpcaps 1
1: =ep

# current bash has no special capabilities
$ cat /proc/$$/status | grep Cap
CapInh: 0000000000000000 i
CapPrm: 0000000000000000 p
CapEff: 0000000000000000 e
CapBnd: 000001ffffffffff
CapAmb: 0000000000000000

# chrony-project is a versatile implementation of the Network Time Protocol (NTP). It can synchronise the system clock with NTP servers, chronyd is a daemon that can be started at boot time.
$ pidof chronyd
55003

$ getpcaps 55003
55003: cap_net_bind_service,cap_sys_time=ep
```

