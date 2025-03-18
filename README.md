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
% cmake -B build
% cd build
% make
```

You need following external packages.

- pkg-config
- libevent > 2.0
- libpcap
- libsodium
- gstreamer-1.0

