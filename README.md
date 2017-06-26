# capnproto-dlang: Cap'n Proto for D

[![Dub version](https://img.shields.io/dub/v/capnproto-dlang.svg)](https://code.dlang.org/packages/capnproto-dlang)
[![Dub downloads](https://img.shields.io/dub/dt/capnproto-dlang.svg)](https://code.dlang.org/packages/capnproto-dlang)
[![Build Status](https://travis-ci.org/capnproto/capnproto-dlang.svg?branch=master)](https://travis-ci.org/capnproto/capnproto-dlang)

[Cap'n Proto](http://capnproto.org) is an extremely efficient protocol for sharing data
and capabilities, and capnproto-dlang is a pure D implementation.

# State

* Passes Cap'n Proto testsuite.
* A little slower/faster than the official C++ implementation (see [benchmarks](#benchmarks)).
* Missing RPC part of Cap'n Proto.
* Missing JSON codec (workaround: capnp tool can convert to and from JSON).
* Missing Cap'n Proto toString format (workaround: capnp tool can convert to and from text format).

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
    auto rootObject = message.initRoot!AnyObject; //AnyObject from example.
    //Do stuff with rootObject.
    //Use Serialize or SerializePacked to get the serialized message.
}
```

## Sample

A full example including pregenerated D code from schema is available [here](https://github.com/capnproto/capnproto-dlang/tree/master/source/samples).

```bash
dub build -c sample-addressbook
[capnproto-dlang]$ ./addressbook write | ./addressbook read
Alice: alice@example.com
  mobile phone: 555-1212
  student at: MIT
Bob: bob@example.com
  home phone: 555-4567
  work phone: 555-7654
  unemployed
```

# <a name="benchmarks"></a>Benchmarks

Benchmarked on Skylake i7. Best of three runs.

```bash
dub build -c benchmark-carsales --compiler ldc --build=release

[capnproto-dlang]$ time ./benchmark-carsales object 0 none 20000
real    0m0,538s
user    0m0,527s
sys     0m0,010s

[capnproto-c++]$ time ./capnproto-carsales object no-reuse none 20000
real    0m0,410s
user    0m0,406s
sys     0m0,001s

[capnproto-c++]$ time ./capnproto-carsales object reuse none 20000
real    0m0,350s
user    0m0,346s
sys     0m0,002s

dub build -c benchmark-catrank --compiler ldc --build=release

[capnproto-dlang]$ time ./benchmark-catrank object 0 none 20000
real    0m10,999s
user    0m10,977s
sys     0m0,004s

[capnproto-c++]$ time ./capnproto-catrank object no-reuse none 20000
real    0m11,259s
user    0m10,789s
sys     0m0,422s

[capnproto-c++]$ time ./capnproto-catrank object reuse none 20000
real    0m10,287s
user    0m10,251s
sys     0m0,003s

dub build -c benchmark-eval --compiler ldc --build=release

[capnproto-dlang]$ time ./benchmark-eval object 0 none 20000
real    0m0,109s
user    0m0,105s
sys     0m0,004s

[capnproto-c++]$ time ./capnproto-eval object no-reuse none 20000
real    0m0,191s
user    0m0,189s
sys     0m0,002s

[capnproto-c++]$ time ./capnproto-eval object reuse none 20000
real    0m0,185s
user    0m0,183s
sys     0m0,001s

```
