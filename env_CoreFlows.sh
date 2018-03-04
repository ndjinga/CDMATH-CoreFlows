#!/bin/bash

export CoreFlows_INSTALL=@CMAKE_INSTALL_PREFIX@
export CDMATH_DIR=@CDMATH_DIR@
export PETSC_DIR=@PETSC_DIR@
export PETSC_ARCH="@PETSC_ARCH@"

source $CDMATH_DIR/env_CDMATH.sh

#------------------------------------------------------------------------------------------------------------------- 
export CoreFlows=$CoreFlows_INSTALL/bin/Executable/CoreFlowsMainExe
export LD_LIBRARY_PATH=$PETSC_DIR/$PETSC_ARCH/lib:${PETSC_DIR}/lib:/usr/lib64/:$CoreFlows_INSTALL/lib:${LD_LIBRARY_PATH}
export PYTHONPATH=$CoreFlows_INSTALL/lib:$CoreFlows_INSTALL/lib/CoreFlows_Python:$CoreFlows_INSTALL/bin/CoreFlows_Python:$CoreFlows_INSTALL/lib/python2.7/site-packages/salome:${PYTHONPATH}
export CoreFlowsGUI=$CoreFlows_INSTALL/bin/salome/CoreFlows_Standalone.py
