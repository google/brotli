#!/bin/bash

case "$1" in
    "before_install")
	case "${TRAVIS_OS_NAME}" in
	    "linux")
		case "${BUILD_SYSTEM}" in
		    "bazel")
			wget https://github.com/bazelbuild/bazel/releases/download/0.4.5/bazel_0.4.5-linux-x86_64.deb
			echo 'b494d0a413e4703b6cd5312403bea4d92246d6425b3be68c9bfbeb8cc4db8a55  bazel_0.4.5-linux-x86_64.deb' | sha256sum -c --strict || exit 1
			sudo dpkg -i bazel_0.4.5-linux-x86_64.deb
			;;
		esac
		;;
	esac
	;;
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
		    "bazel")
			brew install bazel
			;;
		esac
		;;
	    "linux")
		case "${CC}" in
		    "pgcc")
			wget 'https://raw.githubusercontent.com/nemequ/pgi-travis/de6212d94fd0e7d07a6ef730c23548c337c436a7/install-pgi.sh'
			echo 'acd3ef995ad93cfb87d26f65147395dcbedd4c3c844ee6ec39616f1a347c8df5  install-pgi.sh' | sha256sum -c --strict || exit 1
			/bin/sh install-pgi.sh
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
	    "maven")
		cd java/org/brotli
		mvn install && cd integration && mvn verify
		;;
	    "bazel")
		bazel test -c opt ...:all
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
    "before_deploy")
	case "${BUILD_SYSTEM}" in
	    "bazel")
		export RELEASE_DATE=`date +%Y-%m-%d`
		perl -p -i -e 's/\$\{([^}]+)\}/defined $ENV{$1} ? $ENV{$1} : $&/eg' .bintray.json
		zip -j9 brotli.zip bazel-bin/libbrotli*.a bazel-bin/libbrotli*.so bazel-bin/bro
		;;
	esac
	;;
esac
