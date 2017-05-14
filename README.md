tinygain.lv2 - minimalistic gain control with small MOD GUI
===========================================================

A simple gain control for http://moddevices.com/
that needs little GUI screen estate.

Install
-------

Compiling tinygain.lv2 requires the LV2 SDK, gnu-make, and a c-compiler.

```bash
  git clone git://github.com/x42/tinygain.lv2.git
  cd tinygain.lv2
  make
  sudo make install PREFIX=/usr
```

To build the the MOD GUI use `make MOD=1`
