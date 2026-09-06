#!/bin/bash
#set -e # exit on first error

export ROOT=`pwd`
export NCORES=`nproc --all`
export CMAKE_BUILD_PARALLEL_LEVEL=${NCORES}
# An active conda/miniforge environment prepends its own bin/ to PATH, and
# CMake's find_package(... CONFIG) search treats PATH entries as candidate
# install prefixes -- so a conda env can silently shadow a perfectly good
# Fedora system library (e.g. zstd, used transitively by Boost::iostreams)
# with an older/incompatible copy, baking that path into the built binary's
# RPATH. Strip any active conda environment's bin dirs from PATH so the
# Fedora system toolchain and libraries are used consistently.
if [[ -n "$CONDA_PREFIX" ]]; then
    PATH=$(echo "$PATH" | tr ':' '\n' | grep -vF "$CONDA_PREFIX" | paste -sd:)
fi
# Several bundled/third-party dependencies (and their own nested try_compile()
# checks) declare cmake_minimum_required() below 3.5, which newer CMake
# (>=4.0) refuses to configure at all. Setting this env var (rather than only
# passing -DCMAKE_POLICY_VERSION_MINIMUM on the command line) makes CMake
# auto-apply it to every nested build tree it creates, not just the top level.
export CMAKE_POLICY_VERSION_MINIMUM=3.5
#FOUND_GTK2=$(dnf list installed | grep gtk2)
#FOUND_GTK3=$(dnf list installed | grep gtk3)
FOUND_GTK3=1

function check_available_memory_and_disk() {
    FREE_MEM_GB=$(free -g -t | grep 'Mem:' | rev | cut -d" " -f1 | rev)
    MIN_MEM_GB=4

    FREE_DISK_KB=$(df -k . | tail -1 | awk '{print $4}')
    MIN_DISK_KB=$((10 * 1024 * 1024))

    if [ ${FREE_MEM_GB} -le ${MIN_MEM_GB} ]; then
        echo -e "\nERROR: QIDI Studio Builder requires at least ${MIN_MEM_GB}G of 'available' mem (systen has only ${FREE_MEM_GB}G available)"
        echo && free -h && echo
        exit 2
    fi

    if [[ ${FREE_DISK_KB} -le ${MIN_DISK_KB} ]]; then
        echo -e "\nERROR: QIDI Studio Builder requires at least $(echo $MIN_DISK_KB |awk '{ printf "%.1fG\n", $1/1024/1024; }') (systen has only $(echo ${FREE_DISK_KB} | awk '{ printf "%.1fG\n", $1/1024/1024; }') disk free)"
        echo && df -h . && echo
        exit 1
    fi
}

unset name
while getopts ":dsiuhgbr" opt; do
  case ${opt} in
    u )
        UPDATE_LIB="1"
        ;;
    i )
        BUILD_IMAGE="1"
        ;;
    d )
        BUILD_DEPS="1"
        ;;
    s )
        BUILD_QIDI_STUDIO="1"
        ;;
    b )
        BUILD_DEBUG="1"
        ;;
    g )
        FOUND_GTK3=""
        ;;
    r )
	SKIP_RAM_CHECK="1"
	;;
    h ) echo "Usage: ./BuildFedora.sh [-i][-u][-d][-s][-b][-g]"
        echo "   -i: Generate appimage (optional)"
        echo "   -g: force gtk2 build"
        echo "   -b: build in debug mode"
        echo "   -d: build deps (optional)"
        echo "   -s: build qidi-studio (optional)"
        echo "   -u: only update clock & dependency packets (optional and need sudo)"
	echo "   -r: skip free ram check (low ram compiling)"
        echo "For a first use, you want to 'sudo ./BuildFedora.sh -u'"
        echo "   and then './BuildFedora.sh -dsi'"
        exit 0
        ;;
  esac
done

if [ $OPTIND -eq 1 ]
then
    echo "Usage: ./BuildFedora.sh [-i][-u][-d][-s][-b][-g]"
    echo "   -i: Generate appimage (optional)"
    echo "   -g: force gtk2 build"
    echo "   -b: build in debug mode"
    echo "   -d: build deps (optional)"
    echo "   -s: build qidi-studio (optional)"
    echo "   -u: only update clock & dependency packets (optional and need sudo)"
    echo "   -r: skip free ram check (low ram compiling)"
    echo "For a first use, you want to 'sudo ./BuildFedora.sh -u'"
    echo "   and then './BuildFedora.sh -dsi'"
    exit 0
fi

# mkdir build
if [ ! -d "build" ]
then
    mkdir build
fi

# Addtional Dev packages for QIDIStudio
#export REQUIRED_DEV_PACKAGES="libmspack-dev libgstreamerd-3-dev libsecret-1-dev libwebkit2gtk-4.0-dev libosmesa6-dev libssl-dev libcurl4-openssl-dev eglexternalplatform-dev libudev-dev libdbus-1-dev extra-cmake-modules"
# libwebkit2gtk-4.1-dev ??
#export DEV_PACKAGES_COUNT=$(echo ${REQUIRED_DEV_PACKAGES} | wc -w)
#if [ $(dpkg --get-selections | grep -E "$(echo ${REQUIRED_DEV_PACKAGES} | tr ' ' '|')" | wc -l) -lt ${DEV_PACKAGES_COUNT} ]; then
#    sudo apt install -y ${REQUIRED_DEV_PACKAGES} git cmake wget file
#fi

#FIXME: require root for -u option
if [[ -n "$UPDATE_LIB" ]]
then
    echo -n -e "Updating linux ...\n"
    # hwclock -s # DeftDawg: Why does SuperSlicer want to do this?
    dnf makecache
    # NOTE: cereal is intentionally NOT installed from Fedora's cereal-devel here.
    # deps/CMakeLists.txt always builds its own bundled cereal (no toggle to skip
    # it, unlike GLFW), and it exports an imported target literally named
    # `cereal`. Fedora's cereal-devel instead exports the namespaced
    # `cereal::cereal`, which this project's target_link_libraries(... cereal)
    # calls don't match -- if cereal_DIR resolves to the system package, CMake
    # silently treats the unmatched "cereal" as a raw "-lcereal" linker flag,
    # which fails since there is no such standalone library. Only the
    # deps-built cereal-config.cmake is compatible with this codebase.
    if [[ -z "$FOUND_GTK3" ]]
    then
        echo -e "\nInstalling: gtk2-devel glew-devel systemd-devel dbus-devel cmake git glfw-devel NLopt-devel openvdb-devel imath-devel openexr-devel nasm webkit2gtk4.1-devel boost-static mesa-compat-libOSMesa-devel\n"
        dnf install -y gtk2-devel glew-devel systemd-devel dbus-devel cmake git glfw-devel NLopt-devel openvdb-devel imath-devel openexr-devel nasm webkit2gtk4.1-devel boost-static mesa-compat-libOSMesa-devel
    else
        echo -e "\nFind gtk3-devel, installing: gtk3-devel glew-devel systemd-devel dbus-devel cmake git glfw-devel NLopt-devel openvdb-devel imath-devel openexr-devel nasm webkit2gtk4.1-devel boost-static mesa-compat-libOSMesa-devel\n"
        dnf install -y gtk3-devel glew-devel systemd-devel dbus-devel cmake git glfw-devel NLopt-devel openvdb-devel imath-devel openexr-devel nasm webkit2gtk4.1-devel boost-static mesa-compat-libOSMesa-devel
    fi
    if [[ -n "$BUILD_DEBUG" ]]
    then
        echo -e "\nInstalling: openssl-devel libcurl-devel\n"
        dnf install -y openssl-devel libcurl-devel
    fi
    echo -e "done\n"
    exit 0
fi

#FOUND_GTK2_DEV=$(dnf list installed | grep gtk2-dev || echo '')
#FOUND_GTK3_DEV=$(dnf list installed | grep gtk3-dev || echo '')
FOUND_GTK3_DEV=1
echo "FOUND_GTK2=$FOUND_GTK2)"
if [[ -z "$FOUND_GTK2_DEV" ]]
then
if [[ -z "$FOUND_GTK3_DEV" ]]
then
    echo "Error, you must install the dependencies before."
    echo "Use option -u with sudo"
    exit 0
fi
fi

echo "[1/9] Updating submodules..."
{
    # update submodule profiles
    pushd resources/profiles
    git submodule update --init
    popd
}

echo "[2/9] Changing date in version..."
{
    # change date in version
    sed -i "s/+UNKNOWN/_$(date '+%F')/" version.inc
}
echo "done"

# mkdir in deps
if [ ! -d "deps/build" ]
then
    mkdir deps/build
fi

if ! [[ -n "$SKIP_RAM_CHECK" ]]
then
check_available_memory_and_disk
fi

if [[ -n "$BUILD_DEPS" ]]
then
    echo "[3/9] Configuring dependencies..."
    # Use Fedora's glfw-devel (ships a proper glfw3Config.cmake) instead of
    # building GLFW from source, which on Linux additionally requires KDE's
    # extra-cmake-modules (ECM) for Wayland support that we don't otherwise need.
    #
    # Also skip building OpenVDB (and its OpenEXR/Imath dependency) here: the
    # main app build is configured to use Fedora's system openvdb-devel
    # instead (see SLIC3R_STATIC_EXCLUDE_OPENVDB below), since Fedora's
    # openvdb-devel has no static .a to link against. But all deps share one
    # install prefix/include root, so if deps ALSO installed its own (older,
    # ABI-incompatible) OpenVDB headers here, the compiler would pick those up
    # instead of Fedora's system headers regardless of any CMake variable
    # override, causing undefined-symbol link errors from an ABI mismatch.
    #
    # Also skip building expat here: the final QIDIStudio link pulls in
    # Fedora's system libwebkit2gtk-4.1.so (for wxWebView), which itself
    # needs a matching dynamic libexpat.so to resolve its own XML_* symbols.
    # If the app instead links deps' own static libexpat.a, those symbols
    # go unresolved and the final link fails.
    BUILD_ARGS="-DDEP_BUILD_GLFW=OFF -DDEP_BUILD_OPENVDB=OFF -DDEP_BUILD_EXPAT=OFF"
    if [[ -n "$FOUND_GTK3_DEV" ]]
    then
        BUILD_ARGS="${BUILD_ARGS} -DDEP_WX_GTK3=ON"
    fi
    if [[ -n "$BUILD_DEBUG" ]]
    then
        # have to build deps with debug & release or the cmake won't find evrything it needs
        mkdir deps/build/release
        pushd deps/build/release
            cmake ../.. -DDESTDIR="../destdir" $BUILD_ARGS -DJPEG_VERSION=6
            make -j$NCORES
        popd
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TYPE=Debug"
    fi
    # cmake deps
    pushd deps/build
        cmake .. $BUILD_ARGS
        echo "done"

        # make deps
        echo "[4/9] Building dependencies..."
        make -j$NCORES
        echo "done"

        # rename wxscintilla # TODO: DeftDawg: Does QIDIStudio need this?
        # echo "[5/9] Renaming wxscintilla library..."
        # pushd destdir/usr/local/lib
        #     if [[ -z "$FOUND_GTK3_DEV" ]]
        #     then
        #         cp libwxscintilla-3.1.a libwx_gtk2u_scintilla-3.1.a
        #     else
        #         cp libwxscintilla-3.1.a libwx_gtk3u_scintilla-3.1.a
        #     fi
        # popd
        # echo "done"

        # FIXME: only clean deps if compiling succeeds; otherwise reruns waste tonnes of time!
        # clean deps
        # echo "[6/9] Cleaning dependencies..."
        # rm -rf dep_*
    popd
    echo "done"
fi

if [[ -n "$BUILD_QIDI_STUDIO" ]]
then
    echo "[7/9] Configuring Slic3r..."
    # Fedora's glew-devel/openvdb-devel packages only ship shared .so libs, no
    # static .a, so linking them statically (the SLIC3R_STATIC default) always
    # fails to find them.
    BUILD_ARGS="-DSLIC3R_STATIC_EXCLUDE_GLEW=1 -DSLIC3R_STATIC_EXCLUDE_OPENVDB=1"
    # wx-config (generated by the bundled wxWidgets build) hardcodes "-L.../lib"
    # for its own dependencies, e.g. "-Wl,-Bstatic -lzlibstatic". But zlib's own
    # CMakeLists installs via GNUInstallDirs, which resolves to "lib64" on
    # Fedora, so that -lzlibstatic can't otherwise be found. Add lib64 as an
    # extra linker search path to cover this dep-install-layout mismatch.
    BUILD_ARGS="${BUILD_ARGS} -DCMAKE_EXE_LINKER_FLAGS=-L$PWD/deps/build/destdir/usr/local/lib64"
    if [[ -n "$FOUND_GTK3_DEV" ]]
    then
        BUILD_ARGS="${BUILD_ARGS} -DSLIC3R_GTK=3"
    fi
    if [[ -n "$BUILD_DEBUG" ]]
    then
        BUILD_ARGS="${BUILD_ARGS} -DCMAKE_BUILD_TYPE=Debug -DQDT_INTERNAL_TESTING=1"
    else
        BUILD_ARGS="${BUILD_ARGS} -DQDT_RELEASE_TO_PUBLIC=1 -DQDT_INTERNAL_TESTING=0"
    fi

    # cmake
    pushd build
        cmake .. -DCMAKE_PREFIX_PATH="$PWD/../deps/build/destdir/usr/local" -DSLIC3R_STATIC=1 ${BUILD_ARGS}
        echo "done"

        # make Slic3r
        echo "[8/9] Building Slic3r..."
        make -j$NCORES QIDIStudio

        # make .mo
        # make gettext_po_to_mo # FIXME: DeftDawg: complains about msgfmt not existing even in SuperSlicer, did this ever work?
    popd
    echo "done"
fi

if [[ -e $ROOT/build/src/BuildLinuxImage.sh ]]; then
# Give proper permissions to script
chmod 755 $ROOT/build/src/BuildLinuxImage.sh

echo "[9/9] Generating Linux app..."
    pushd build
        if [[ -n "$BUILD_IMAGE" ]]
        then
            $ROOT/build/src/BuildLinuxImage.sh -i
        else
            #$ROOT/build/src/BuildLinuxImage.sh
	    echo "skip build imagte"
        fi
    popd
echo "done"
fi
