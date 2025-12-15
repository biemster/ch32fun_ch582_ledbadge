#include <stdio.h>
#include "ch32fun.h"

#define WAKEUP_INTERVAL_MS (3 * 1000) // 3 seconds

#define LED                PA8 // DEBUG led on board, not on badge
#define PIN_CHARGE_STT     PA0
#define PIN_KEY1           PA1
#define PIN_KEY1_INT       PIN_KEY1
#define PIN_KEY2           PB22
#define PIN_KEY2_INT       PB8 // ch582 is weird, PB22 interrupt comes in on PB8 bit in irq flag
#define ADC_VBAT_CHANNEL   14

typedef enum {
	ADC_FREQ_DIV_10 = 0b00,		// 32/10 = 3.2MHz
	ADC_FREQ_DIV_4 = 0b01,		// 32/4 = 8MHz
	ADC_FREQ_DIV_6 = 0b10,		// 32/6 = 5.33MHz - Default
	ADC_FREQ_DIV_8 = 0b11		// 32/8 = 4MHz
} ADC_FREQ_DIV_t;

typedef enum {
	ADC_PGA_GAIN_1_4 = 0b00,	// -12dB 1/4 	range: 2.9V ~ VIO33
	ADC_PGA_GAIN_1_2 = 0b01,	// -6dB 1/2 	range: 1.9V ~ 3V
	ADC_PGA_GAIN_1 = 0b10,		// 0dB 1 		range: 0V ~ 2V - Default
	ADC_PGA_GAIN_2 = 0b11		// 6dB 2		range: 0.6V ~ 1.5V
} ADC_PGA_GAIN_t;

typedef enum {
	IRQ_NONE = 0,
	IRQ_RTC,
	IRQ_KEY1,
	IRQ_KEY2,
} irq_source;

static volatile irq_source wakeup_source;
static volatile int gs_vbat_mV;


void blink(int n) {
	for(int i = n-1; i >= 0; i--) {
		funDigitalWrite( LED, FUN_LOW ); // Turn on LED
		LowPowerIdle( MS_TO_RTC(33) ); // why was this necessary again, instead of Delay_Ms, when sleeping?
		funDigitalWrite( LED, FUN_HIGH ); // Turn off LED
		if(i) LowPowerIdle( MS_TO_RTC(33) );
	}
}


__INTERRUPT
void GPIOA_IRQHandler() {
	int status = R16_PA_INT_IF;
	R16_PA_INT_IF = status; // acknowledge

	if(status & PIN_KEY1) {
		wakeup_source = IRQ_KEY1;
	}
}

__INTERRUPT
void GPIOB_IRQHandler() {
	int status = R16_PB_INT_IF;
	R16_PB_INT_IF = status; // acknowledge

	if(status & PIN_KEY2_INT) {
		wakeup_source = IRQ_KEY2;
	}
}

__INTERRUPT
void RTC_IRQHandler(void)
{
	// clear trigger flag
	R8_RTC_FLAG_CTRL = RB_RTC_TRIG_CLR;
	wakeup_source = IRQ_RTC;
}

void allPinPullUp(void)
{
	R32_PA_DIR = 0; //Direction input
	R32_PA_PD_DRV = 0; //Disable pull-down
	R32_PA_PU = P_All; //Enable pull-up
#if PB // 582 has PB, but keep this for eventual portability
	R32_PB_DIR = 0; //Direction input
	R32_PB_PD_DRV = 0; //Disable pull-down
	R32_PB_PU = P_All; //Enable pull-up
#endif	
}

void GPIOSetup() {
	allPinPullUp();
	funPinMode( PIN_KEY1, GPIO_CFGLR_IN_PD ); // Set PIN_KEY1 to input, pulldown as keypress is to vcc
	funPinMode( PIN_KEY2, GPIO_CFGLR_IN_PU ); // Set PIN_KEY2 to input, pullup as keypress is to gnd

	// key1 interrupt
	R16_PA_INT_MODE |= (PIN_KEY1_INT); // edge mode, should go to ch32fun.h
	funDigitalWrite(PIN_KEY1, FUN_HIGH); // rising edge

	NVIC_EnableIRQ(GPIOA_IRQn);
	R16_PA_INT_IF = (PIN_KEY1_INT); // reset key1 flag
	R16_PA_INT_EN |= (PIN_KEY1_INT); // enable key1 interrupt

	// key2 interrupt
	R16_PIN_ALTERNATE |= RB_PIN_INTX; // set PB8 interrupt to PB22 (PIN_KEY2_INT points to PB8)
	R16_PB_INT_MODE |= (PIN_KEY2_INT & ~PB); // edge mode, should go to ch32fun.h
	funDigitalWrite(PIN_KEY2, FUN_LOW); // falling edge

	NVIC_EnableIRQ(GPIOB_IRQn);
	R16_PB_INT_IF = (PIN_KEY2_INT & ~PB); // reset key2 flag
	R16_PB_INT_EN |= (PIN_KEY2_INT & ~PB); // enable key2 interrupt

	// enable wakeup on either key
	SYS_SAFE_ACCESS(
		R8_SLP_WAKE_CTRL |= (RB_WAKE_EV_MODE | RB_SLP_GPIO_WAKE);
		R8_SLP_POWER_CTRL &= ~(RB_WAKE_DLY_MOD);
		R8_SLP_POWER_CTRL |= 0x00; // 0x00: long delay, 0x01: short delay (short doesn't work here)
	);
}

void update_battery_voltage_mV() {
	R8_ADC_CHANNEL = ADC_VBAT_CHANNEL;
	R8_TKEY_CFG &= ~RB_TKEY_PWR_ON;
	R8_ADC_CFG = RB_ADC_POWER_ON | (ADC_PGA_GAIN_1_4 << 4) | (ADC_FREQ_DIV_10 << 6);

	R8_ADC_CONVERT |= RB_ADC_START;
	while( R8_ADC_CONVERT & RB_ADC_START );

	gs_vbat_mV = ((1050 * R16_ADC_DATA) / 512) - (1050 * 3); // -12dB: vref * (raw/512 - 3)
}


int main() {
	SystemInit();

	// GPIO setup
	funGpioInitAll(); // no-op on ch5xx
	GPIOSetup();
	funPinMode( LED, GPIO_CFGLR_OUT_2Mhz_PP ); // DEBUG led on board, not on badge

	// Sleep setup
	DCDCEnable(); // Enable the internal DCDC
	LSIEnable(); // Disable LSE, enable LSI
	RTCInit(); // Set the RTC counter to 0 and enable RTC Trigger
	SleepInit(); // Enable wakeup from sleep by RTC, and enable RTC IRQ

	// Some one-off startup stuff
	blink(5);

	while(1) {
		// only housekeeping here, we go to sleep for a bit after this
		switch(wakeup_source) {
		case IRQ_RTC:
			blink(1);
			update_battery_voltage_mV();
			break;
		case IRQ_KEY1:
			break;
		case IRQ_KEY2:
			blink(2);
			jump_isprom();
			break;
		case IRQ_NONE: // fall-through
		default:
			break;
		}

		// closing matters
		wakeup_source = IRQ_NONE;
		LowPower( MS_TO_RTC(WAKEUP_INTERVAL_MS), (RB_PWR_RAM2K | RB_PWR_RAMX | RB_PWR_EXTEND) );
		DCDCEnable(); // During low power mode, the DCDC is disabled so we need to enable it
	}
}
