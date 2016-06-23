#!/bin/sh -x

OPERATION="$1"

case "${BUILD_SYSTEM}" in
    "cmake")
        case "${OPERATION}" in
            "script")
                mkdir builddir && cd builddir
                cmake -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" ..
                make VERBOSE=1
                make test
                ;;
        esac
        ;;
    "python")
        case "${OPERATION}" in
            "install")
		if [ $TRAVIS_OS_NAME = "osx" ]; then
                    source terryfy/travis_tools.sh
                    get_python_environment $INSTALL_TYPE $PYTHON_VERSION venv
                    pip install --upgrade wheel
		else
		    pip install --user --upgrade wheel
		fi
                ;;
            "script")
                python setup.py build_ext test
                ;;
            "after_success")
                pip wheel -w dist .
                ;;
        esac
        ;;
esac
