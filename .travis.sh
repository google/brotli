#!/bin/bash

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
		CMAKE_FLAGS=
		if [ "${CROSS_COMPILE}" = "yes" ]; then
		    CMAKE_FLAGS="-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_RC_COMPILER=${RC_COMPILER}"
		fi
		cmake ${CMAKE_FLAGS} -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DENABLE_SANITIZER="${SANITIZER}" -DCMAKE_C_FLAGS="${CFLAGS}" ..
		make VERBOSE=1
		ctest -V
		;;
	    "python")
		if [ "${TRAVIS_OS_NAME}" = "osx" ]; then
			source venv/bin/activate
		fi
		python setup.py build test
		;;
	esac
	;;
    "after_success")
	case "${BUILD_SYSTEM}" in
	    "python")
		case "${TRAVIS_OS_NAME}" in
		    "osx")
			source venv/bin/activate
			pip wheel -w dist .
			;;
		esac
		;;
	esac
	;;
esac
