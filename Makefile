all : flash

TARGET:=ledbadge
TARGET_MCU:=CH582
TARGET_MCU_PACKAGE:=CH582M

include ../ch32fun/ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean
