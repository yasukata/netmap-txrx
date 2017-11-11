# netmap TX/RX example

Simplified netmap pkt-gen.

## How to build

Please specify the path of your netmap directory.

```
$ make SRCDIR=/PATH_TO_NETMAP
```

## How to run

`-i` option specifies the netmap interface.
`-f` option specifies TX or RX.

Receiver

```
$ ./netmap-txrx -i vale0:rx -f rx
```

Sender

```
./netmap-txrx -i vale0:tx -f tx
```

Every 1 second, the sender transmits 3 packets.
Each packet header has following addresses.

* Destination MAC : ff:ff:ff:ff:ff:ff
* Source MAC      : aa:bb:cc:dd:ee:ff
* Destination IP  : 10.0.0.1
* Source IP       : 10.0.0.2
