Yet Another implementation of WifiBroadcast NG protocol.

Only simple rx is supported at this time.

Main features:
--------------
- receive wfb-ng packet using libpcap.
- parse, decrypt, and decode FEC
- decode H.265 stream using gstreamer library.
- redistribute received frames to IP multicasting network.

## Original Implementation
If you are interested in WFB-ng, see original repository.
- https://github.com/svpcom/wfb-ng

How to Compile:
---------------
This project will be compiled using cmake.

```
% git submodule init
% git submodule update
% cmake -B build 
% cd build
% make
```

if you have gstreamer-1.0, you can enable GStreamer options
by cmake option.

```
% git submodule init
% git submodule update
% cmake -B build -DENABLE_GSTREAMER=ON
% cd build
% make
```

You need following external packages.

- pkg-config
- libevent > 2.0
- libpcap
- libsodium
- gstreamer-1.0

Usage:
------
## The listener program
```
wfb_listener -- WFB-NG based Rx and Multicast Tx

Synopsis:
        wfb_listener [-w <dev>] [-e <dev>] [-E <dev>]
        [-a <addr>] [-p <port>] [-k <file>]
        [-l] [-m] [-n] [-d] [-h]
Options:
        -w <dev> ... specify Wireless Rx device. default: none
        -e <dev> ... specify Ethernet Rx device. default: none
        -E <dev> ... specify Ethernet Tx device. default: none
        -a <addr> ... specify Multicast address . default: ff02::5742
        -p <port> ... specify Multicast port . default: 5742
        -k <file> ... specify cipher key. default: ./gs.key
        -l ... enable local play. default: disable
        -L ... log file name. default: (none)
        -m ... use RFMonitor mode instead of Promiscous mode.
        -n ... don't apply FEC decode.
        -d ... enable debug output.
        -h ... print help(this).

If tx device is not specified, the progaram decode the stream.
```

### redistribute Wireless frames(from OpenIPC FPV) to multicast network.
```
% wfb_listener -w wlan0 -E eth0
```

### receive multicast packets and play with GStreamer
```
% wfb_listener -e eth0 -l
```

### receive multicast packets and write to a file.
```
% wfb_listener -e eth0 -L output.log
```

## The log analyzer
```
wfb_log_analysis --WFB-YA log analyzer

Synopsis:
        wfb_log_analysis [-f <name>] [-o <name>] [-t <type>] [-l] [-i] [-d]
Options:
        -f <name> ... specify input file name. default: STDIN
        -o <name> ... specify output file name. default: STDOUT
        -t <type> ... specify output file format. default: csv
        -l ... enable local play(GStreamer)
        -i ... interactive mode
        -d ... enable debug log.
Output Foramt <type>:
        csv .. comma separated values(default).
        json .. javascript object(per sequence).
        json_block .. javascript object(per block).
        summary .. summary values.
        mp4 .. write MP4 video.
        none .. no output. error check only.
```

### import log and output to csv
```
% wfb_log_analysis -f output.log -o output.csv -t csv
```

### import log and output to json (grouped by packet sequence)
```
% wfb_log_analysis -f output.log -o output.csv -t json
```

Please use jq or something to get prety print.

### import log and output to json (grouped by block sequence)
```
% wfb_log_analysis -f output.log -o output.csv -t json_block
```

Please use jq or something to get prety print.

### import log and replay with GStreamer
```
% wfb_log_analysis -f output.log -l
```

### import log and write mp4 file with GStreamer
```
% wfb_log_analysis -f output.log -o output.mp4 -t mp4
```

### use simple interactive shell
```
% wfb_log_analysis -i
> load output.log
Loading output.log...
12170 packets loaded.
> stat
Channel ID: 0
FEC_TYPE: VDM_RS (Reed-Solomon over Vandermonde Matrix)
FEC_K: 8
FEC_N: 12
Number of Ethernet Frames: 12170
Number of Ethernet Frames(with dbm): 12170
Maximum dbm: -36
minimum dbm: -47
Number of H.265 Frames: 8185
Maximum frame size: 1215
minimum frame size: 18
Total H.265 bytes: 8692096
Frame recovered using FEC: 72
Number of corrupted blocks: 2
> help
Comands:
ls write show play stat load exit quit help ?
>
...
```

JSON Examples:
--------------
## Grouped by sequence
```
[
  {
    "Key": 600798,
    "BlockIndex": 50066,
    "FragmentIndex": 6,
    "IsParity": false,
    "IsFEC": false,
    "EthernetFrames": "1",
    "H265Frames": "1",
    "Event": [
      {
        "TimeStamp": 0E-9,
        "Type": "Receive",
        "SourceNode": "fe80::2ecf:67ff:fe92:d83e",
        "Frequency": 5180,
        "dbm": -40,
        "DataSize": 1243
      },
      {
        "TimeStamp": 0.005217000,
        "Type": "Decode",
        "dbm": 0,
        "DataSize": 1215
      }
    ]
  },
  {
    "Key": 600799,
    "BlockIndex": 50066,
    "FragmentIndex": 7,
    "IsParity": false,
    "IsFEC": false,
    "EthernetFrames": "1",
    "H265Frames": "1",
... 
```

## Grouped by block index
```
[
  {
    "Key": 50066,
    "HasLostFrame": true,
    "IsFEC": false,
    "EthernetFrames": "6",
    "H265Frames": "2",
    "Event": [
      {
        "TimeStamp": 0E-9,
        "BlockIndex": 50066,
        "FragmentIndex": 6,
        "IsParity": false,
        "Type": "Receive",
        "SourceNode": "fe80::2ecf:67ff:fe92:d83e",
        "Frequency": 5180,
        "dbm": -40,
        "DataSize": 1243
      },
      {
        "TimeStamp": 0.000842000,
        "BlockIndex": 50066,
        "FragmentIndex": 7,
        "IsParity": false,
        "Type": "Receive",
        "SourceNode": "fe80::2ecf:67ff:fe92:d83e",
        "Frequency": 5180,
        "dbm": -40,
        "DataSize": 1243
      },
... 
```
