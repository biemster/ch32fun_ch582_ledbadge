all : flash

TARGET:=ledbadge
TARGET_MCU:=CH582
TARGET_MCU_PACKAGE:=CH582F

include ../ch32fun/ch32fun/ch32fun.mk

bootloader:
	./$(TARGET).py -b

flash : bootloader cv_flash
clean : cv_clean
