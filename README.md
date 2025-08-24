## Memory registration issue reproducer

```
usage: ./mem-reg-test -n <node> -s <service>
options:
 -h, --help               Show this usage information
 -i, --info               Display information about the selected
                          provider and fabric and then exit.
 -p, --provider <name>    Provider name hint, see fabric(7)
                          for information.
 -n, --node <node>        Fabric node address, usually an ip address
                          assigned to the rdma interface.
 -s, --service <service>  Service name/number, usually a port number
 -M, --mmap-flags <flags> Flags passed to mmap.
 -P, --prot-flags <flags> Protection flags passed to mmap.
 -O, --open-flags <flags> Flags passed to open()
 -c, --caps <caps>        Provider caps, see fi_info(7).
                          Default: FI_RMA
 -e, --ep-type <type>     Endpoint type, default is FI_EP_MSG
 -m, --mode <mode>        Fabric mode, default: FI_RX_CQ_DATA
```

To reproduce the issue:
```
./mem-reg-test -n <interface-ip> -s <random-port> \
    --provider verbs \
    --open-flags O_RDONLY \
    --mmap-flags MAP_SHARED,MAP_LOCKED \
    --prot-flags PROT_READ
```

Sanity check:
```
./mem-reg-test -n <interface-ip> -s <random-port> \
    --provider verbs \
    --open-flags O_RDWR \
    --mmap-flags MAP_SHARED,MAP_LOCKED \
    --prot-flags PROT_READ,PROT_WRITE
```
