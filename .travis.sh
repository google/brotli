#!/bin/sh

case "$1" in
    "install")
	case "${TRAVIS_OS_NAME}" in
	    "osx")
		brew update
		brew install binutils

		case "${CC}" in
		    "gcc-"*)
			which ${CC} || brew install homebrew/versions/gcc$(echo "${CC#*-}" | sed 's/\.//')
			;;
		esac

		case "${BUILD_SYSTEM}" in
		    "python")
			source terryfy/travis_tools.sh
			get_python_environment $INSTALL_TYPE $PYTHON_VERSION venv
			pip install --upgrade wheel
			;;
		esac
		;;
	esac
	;;
    "script")
	case "${BUILD_SYSTEM}" in
	    "cmake")
		mkdir builddir && cd builddir
		cmake -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DENABLE_SANITIZER="${SANITIZER}" -DCMAKE_C_FLAGS="${CFLAGS}" ..
		make VERBOSE=1
		ctest -V
		;;
	    "python")
                python setup.py build_ext test
		;;
	esac
	;;
    "after_success")
	case "${BUILD_SYSTEM}" in
	    "python")
		pip wheel -w dist .
		;;
	esac
	;;
esac
