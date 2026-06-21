# Build & install

## Fedora / RHEL

```bash
sudo dnf install gcc make autoconf automake pkgconf-pkg-config sdl3-devel
autoreconf -fiv
./configure
make
sudo make install
```

## Debian / Ubuntu

```bash
sudo apt install build-essential autoconf automake pkg-config libsdl3-dev
autoreconf -fiv
./configure
make
sudo make install
```

If SDL3 is not yet packaged, build it from source first
(https://github.com/libsdl-org/SDL).

## macOS

```bash
brew install autoconf automake pkg-config sdl3
autoreconf -fiv
./configure
make
```

## Run

```bash
./1985 --disk-a /path/to/cpm.dsk
```

`--config PATH` overrides the config file location (the override is
read-only; saved settings still go to `~/.config/1985/1985.conf`).
