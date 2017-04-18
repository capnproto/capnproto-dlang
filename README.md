# capnproto-dlang: Cap'n Proto for D

[![Dub version](https://img.shields.io/dub/v/capnproto-dlang.svg)](https://code.dlang.org/packages/capnproto-dlang)
[![Dub downloads](https://img.shields.io/dub/dt/capnproto-dlang.svg)](https://code.dlang.org/packages/capnproto-dlang)
[![Build Status](https://travis-ci.org/ThomasBrixLarsen/capnproto-dlang.svg?branch=master)](https://travis-ci.org/ThomasBrixLarsen/capnproto-dlang)

[Cap'n Proto](http://capnproto.org) is an extremely efficient protocol for sharing data
and capabilities, and capnproto-dlang is a pure D implementation.

# State

* Passes Cap'n Proto testsuite.
* Optimized. A little slower than the official C++ implementation (see [benchmarks](#benchmarks)).
* Missing RPC part of Cap'n Proto.

# Schema compilation
Build the dlang plugin for the Cap'n Proto compiler.

```bash
make
```

Run the Cap'n Proto compiler to generate the D interface code for your schema.

```bash
capnpc -odlang example.capnp
```

Or

```bash
capnpc -o/path/to/capnpc-dlang example.capnp
```

Depending on whether the dlang plugin is installed to path.

# Use in code

```D
import example;
import capnproto;

void main()
{
    auto message = new MessageBuilder(); //From capnproto.
    auto rootObject = message.initRoot!RootObject; //RootObject from example.
    //Do stuff with rootObject.
    //Use Serialize or SerializePacked to get the serialized message.
}
```

A full example including pregenerated D code from schema is available [here](https://github.com/ThomasBrixLarsen/capnproto-dlang/tree/master/source/samples).

# <a name="benchmarks"></a>Benchmarks

```bash
dub build -c benchmark-carsales --compiler ldc --build=release

[capnproto-dlang]$ time ./benchmark-carsales object 0 none 20000
real    0m0,643s
user    0m0,612s
sys     0m0,030s

[capnproto-c++]$ time ./capnproto-carsales object no-reuse none 20000
real    0m0,410s
user    0m0,406s
sys     0m0,001s

[capnproto-c++]$ time ./capnproto-carsales object reuse none 20000
real    0m0,350s
user    0m0,346s
sys     0m0,002s
```
