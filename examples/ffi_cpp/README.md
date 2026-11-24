# C++ FFI example

Build the test DLL with a C++ compiler, then run the CCPL program.

```bat
cl /nologo /LD /EHsc /O2 ffitest.cpp
coolc use_ffi.ccpl --run
```

With the Visual C++ tools, run the commands from a developer prompt so `cl`
is on `PATH`. The exported decorated names in `use_ffi.ccpl` match this source
file. If you change a signature, list exports with:

```bat
dumpbin /exports ffitest.dll
```

and update the `as` strings in the CCPL program.

The member function receives its object pointer as the first argument. Here the
object is two `f64` values in memory, allocated with `malloc`.