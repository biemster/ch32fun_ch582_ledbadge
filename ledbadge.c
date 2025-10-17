/*
 * This demo jumps to the built-in bootloader, so it can be programmed over ISP.
 * Most ch5xx dev boards come with a "boot" or "download" button, when this
 * is pressed the chip resets and executes the ISP bootloader which presents
 * itself on USB to the host.
 * NOTE: this does not work together with "FUNCONF_USE_USBPRINTF"! For use
 * in combination with that refer to the examples_usb/USBFS/usbfs_cdc_tty demo.
 */

#include <stdio.h>
#include "ch32fun.h"
#include "fsusb.h"

#define PIN_CHARGE_STT PA0
#define PIN_KEY1 PA1
#define PIN_KEY2 PB22
#define KEY1_PRESSED funDigitalRead( PIN_KEY1 )
#define KEY2_PRESSED !funDigitalRead( PIN_KEY2 )

int main() {
	SystemInit();

	funGpioInitAll(); // no-op on ch5xx

	funPinMode( PIN_KEY1 | PIN_KEY2, GPIO_CFGLR_IN_PUPD ); // Set PIN_KEY1,2 to input

	if( KEY2_PRESSED ) {
		jump_isprom();
		while(1);
	}
	USBFSSetup();

	printf("ledbadge\r\n");

	while(1) {
	}
}
