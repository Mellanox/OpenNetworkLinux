###############################################################################
#
# 
#
###############################################################################
THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
x86_64_mlnx_idg4400_INCLUDES := -I $(THIS_DIR)inc
x86_64_mlnx_idg4400_INTERNAL_INCLUDES := -I $(THIS_DIR)src
x86_64_mlnx_idg4400_DEPENDMODULE_ENTRIES := init:x86_64_mlnx_idg4400 ucli:x86_64_mlnx_idg4400

