
# Building QIDI Studio on MacOS

## Enviroment setup

Install Following tools:

- Xcode from app store
- Cmake
- git
- gettext

Cmake, git, gettext can be installed from brew(brew install cmake git gettext)

## Building the dependencies

You need to build the dependencies of QIDIStudio first. (Only needs for the first time)

Suppose you download the codes into `/Users/_username_/work/projects/QIDIStudio`.

Create a directory to store the built dependencies: `/Users/_username_/work/projects/QIDIStudio_dep`.
**(Please make sure to replace the username with the one on your computer)**

Then:

```shell
cd QIDIStudio/deps
mkdir build
cd build
```

Next, for arm64 architecture:
```shell
cmake ../ -DDESTDIR="/Users/username/work/projects/QIDIStudio_dep" -DOPENSSL_ARCH="darwin64-arm64-cc"
make
```

Or, for x86 architeccture:
```shell
cmake ../ -DDESTDIR="/Users/username/work/projects/QIDIStudio_dep" -DOPENSSL_ARCH="darwin64-x86_64-cc"
make -jN
```
(N can be a number between 1 and the max cpu number)  

## Building QIDI Studio

Create a directory to store the installed files at `/Users/username/work/projects/QIDIStudio/install_dir`:

```shell
cd QIDIStudio
mkdir install_dir
mkdir build
cd build
```

To build using CMake:

```shell
cmake ..  -DQDT_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="/Users/username/work/projects/QIDIStudio_dep/usr/local" -DCMAKE_INSTALL_PREFIX="../install_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_RPATH="/Users/username/work/projects/QIDIStudio_dep/usr/local" -DCMAKE_MACOSX_BUNDLE=on
cmake --build . --target install --config Release -jN
```

To build for use with XCode:

```shell
cmake .. -GXcode -DQDT_RELEASE_TO_PUBLIC=1 -DCMAKE_PREFIX_PATH="/Users/username/work/projects/QIDIStudio_dep/usr/local" -DCMAKE_INSTALL_PREFIX="../install_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MACOSX_RPATH=ON -DCMAKE_INSTALL_RPATH="/Users/username/work/projects/QIDIStudio_dep/usr/local" -DCMAKE_MACOSX_BUNDLE=on
```
