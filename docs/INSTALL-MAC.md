# MacOS
There are two main "package managers" used on MacOS: Homebrew and MacPorts. Trunk-recorder can be installed with dependencies from one or the other

## Using Homebrew
### Install Homebrew
See [the Brew homepage](https://brew.sh) for more information.
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install.sh)"
```

### Install GNURadio and other dependencies
Install GNURadio, the OsmoSDR package for GNURadio, CMake, pkgconfig, cppunit, and openssl through Homebrew:
```bash
brew install gnuradio uhd gr-osmosdr cmake pkgconfig cppunit openssl fdk-aac-encoder sox
```
Note that you will need to provide the flag `-DOPENSSL_ROOT_DIR=/usr/local/opt/openssl` to the invocation of `cmake` in the [Build Instructions](https://github.com/robotastic/trunk-recorder/wiki/Building-Trunk-Recorder) or you will receive an error from CMake about not finding libssl or a linking error from `make` about not having a library for `-lssl`:
```bash
cmake ../trunk-recorder -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl
```
## Using MacPorts
### These instructions should work on OS X 10.10, OS X 10.11, and macOS 10.12.

#### Install MacPorts 

Follow [the instructions from the MacPorts project to install the appropriate version of MacPorts](https://www.macports.org/install.php) for your version of macOS.

If you have already installed MacPorts, make sure your ports tree is up to date:
```bash
sudo port selfupdate
```

*(7/24/21) Note: this has been tested and works on an M1 based Mac. Some dependencies for **gr-osmosdr** do not support ARM64 yet and can be removed by adding a -, eg: -docs*

#### Install GNU Radio

The preferred method for [installing GNU Radio](http://gnuradio.org/redmine/projects/gnuradio/wiki/InstallingGR) on macOS is: 
 
```bash
sudo port install gnuradio uhd gr-osmosdr
```

#### Install tools to compile Trunk Recorder
```bash
sudo port install cmake boost libusb cppunit
```

#### Install tools for OpenMHz
If you are interested in uploading recordings to OpenMHz, install FDK-AAC and Sox  to convert the Wav files to M4a.
```bash
sudo port install sox
```

Download and make [libfdk-aac](https://github.com/mstorsjo/fdk-aac).
extract the source, and cd to the source directory

```bash

autoreconf -i
./configure
make
sudo make install
```

Download and make the command line [**fdkaac** program](https://github.com/nu774/fdkaac).
extract the source, and cd to the source directory
```bash
autoreconf -i
./configure
make
sudo make install
```

## Configuring Trunk Recorder

The next step is to [configure Trunk Recorder](CONFIGURE.md) for the system you are trying to capture.

## Running trunk recorder. 

If all goes well you should now have the executable named `trunk-recorder`, and created the `config.json` configuration file as described in the [Wiki](https://github.com/robotastic/trunk-recorder/wiki/Configuring-a-System) and [README](https://github.com/robotastic/trunk-recorder/blob/master/README.md#configure).

From your build directory (e.g. `trunk-build`) you can now run
`./trunk-recorder`

### Runtime options

Trunk Recorder will look for a *config.json* file in the same directory as it is being run in. You can point it to a different config file by using the *--config* argument on the command line, for example: `./trunk-recorder --config=examples/config-wmata-rtl.json`.
