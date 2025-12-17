#include <stdio.h>
#include "ch32fun.h"
#include "iSLER.h"

#define MATRIX_NROW        11
#define MATRIX_NCOL        44
#define WAKEUP_INTERVAL_MS (3 * 1000) // 3 seconds
#define FB_NCOL            (MATRIX_NCOL /2) // number of columns in framebuffer

#define PIN_CHARGE_STT     PA0
#define PIN_CHARGE_STT_INT PIN_CHARGE_STT
#define PIN_KEY1           PA1
#define PIN_KEY1_INT       PIN_KEY1
#define PIN_KEY2           PB22
#define PIN_KEY2_INT       PB8 // ch582 is weird, PB22 interrupt comes in on PB8 bit in irq flag

#define ADC_VBAT_CHANNEL   14
#define VBAT_100           3500 // mV 100% (the chip is powered through a voltage divider, corresponds to ~4200mV)
#define VBAT_0             2900 // mV 0% (the chip is powered through a voltage divider, corresponds to ~3500mV)

#define ACCESS_ADDRESS     0x8E89BED6 // the "BED6" address for BLE advertisements
#define ROM_CFG_MAC_ADDR   0x7F018 // should go to ch5xxhw.h

#define IS_CHARGING        (funDigitalRead(PIN_CHARGE_STT) == 0)


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
	IRQ_CHARGE_STT,
} irq_source;

static volatile irq_source wakeup_source;
static volatile int gs_vbat_mV;
static volatile uint32_t framebuffer[FB_NCOL];

__attribute__((aligned(4))) uint8_t adv[] = {
		0x02, 0x16, // header for LL: PDU + frame length
		0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // MAC (reversed)
		0x04, 0x16, 0x0F, 0x18, 55, // battery (55%, note the missing 0x)
		0x09, 0x09, 'l','e','d','b','a','d','g','e'}; // 0x09: "Complete Local Name"
uint8_t adv_channels[] = {37,38,39};

#define LINE_A PA15
#define LINE_B PB18
#define LINE_C PB0
#define LINE_D PB7
#define LINE_E PA12
#define LINE_F PA10
#define LINE_G PA11
#define LINE_H PB9
#define LINE_I PB8
#define LINE_J PB15
#define LINE_K PB14
#define LINE_L PB13
#define LINE_M PB12
#define LINE_N PB5
#define LINE_O PA4
#define LINE_P PB3
#define LINE_Q PB4
#define LINE_R PB2
#define LINE_S PB1
#define LINE_T PB6 // or PB23
#define LINE_U PB21
#define LINE_V PB20
#define LINE_W PB19

static u32 led_lines[] = {
	LINE_A, LINE_B, LINE_C, LINE_D, LINE_E,
	LINE_F, LINE_G, LINE_H, LINE_I, LINE_J,
	LINE_K, LINE_L, LINE_M, LINE_N, LINE_O,
	LINE_P, LINE_Q, LINE_R, LINE_S, LINE_T,
	LINE_U, LINE_V, LINE_W
};
#define NLINES (sizeof(led_lines) / sizeof(u32))


//      0     1     2     3     4     5     6     7     8     9
//   -----------------------------------------------------------------
// 0 | 0,2 | 0,1 | 1,0 | 1,2 | 2,0 | 2,1 | 3,0 | 3,1 | 4,0 | 4,1 | ...
//   -----------------------------------------------------------------
// 1 | 0,3 | 0,4 | 1,3 | 1,4 | 2,3 | 2,4 | 3,2 | 3,4 | 4,2 | 4,3 | ...
//   -----------------------------------------------------------------
// 2 | 0,5 | 0,6 | 1,5 | 1,6 | 2,5 | 2,6 | 3,5 | 3,6 | 4,5 | 4,6 | 5,4 | 5,6 | 6,4 | 6,5
//   -----------------------------------------------------------------
// 3 | 0,7 | 0,8 | ...
//
int xy_to_yline(int x, int y) {
	// wrap around
	x %= MATRIX_NCOL;
	y %= MATRIX_NROW;

	// [0,0] and [1,0] are swapped
	if(y == 0) {
		x = (x == 0) ? 1:
			(x == 1) ? 0:
					   x;
	}

	// translate x,y to led line
	int x_line = x / 2;
	int y_line = y * 2;
	if(x % 2) {
		y_line++;
	}
	if(x_line <= y_line) {
		y_line++;
	}

	return y_line;
}

void blink_single(int ms, int x, int y) {
	for(int i = 0; i < NLINES; i++) {
		funPinMode( led_lines[i], GPIO_CFGLR_IN_FLOAT );
	}

	int x_line = x / 2;
	int y_line = xy_to_yline(x,y);

	funPinMode( led_lines[x_line], GPIO_CFGLR_OUT_2Mhz_PP );
	funPinMode( led_lines[y_line], GPIO_CFGLR_OUT_2Mhz_PP );
	funDigitalWrite(led_lines[x_line], FUN_HIGH);
	funDigitalWrite(led_lines[y_line], FUN_LOW);
	LowPowerIdle( MS_TO_RTC(ms) );
	funPinMode( led_lines[x_line], GPIO_CFGLR_IN_FLOAT );
	funPinMode( led_lines[y_line], GPIO_CFGLR_IN_FLOAT );
}

// 2 columns are connected to a single x coordinate, so we can
// control 2 columns by holding a single pin HIGH.
// the first 11 bits in y_coords correspond to y = {0,10} of the even
// x coordinate, the second 11 bits to the y's of the odd x coordinate
void blink_2col(int ms, int col, uint32_t y_coords) {
	for(int i = 0; i < NLINES; i++) {
		funPinMode( led_lines[i], GPIO_CFGLR_IN_FLOAT );
	}

	// first bring up the x coordinates
	funPinMode( led_lines[col], GPIO_CFGLR_OUT_2Mhz_PP );
	funDigitalWrite(led_lines[col], FUN_HIGH);

	// now gnd the y coordinates
	int x = col * 2;
	for(int i = 0; i < (2*MATRIX_NCOL); i++) {
		if(y_coords & 1) {
			int y_line = xy_to_yline(x,i);
			funPinMode( led_lines[y_line], GPIO_CFGLR_OUT_2Mhz_PP );
			funDigitalWrite(led_lines[y_line], FUN_LOW);
		}
		x = (i == 10) ? x+1 : x; // go to next x after 11 ys
		y_coords >>= 1;
	}

	if(ms) {
		LowPowerIdle( MS_TO_RTC(ms) );
	}

	for(int i = 0; i < NLINES; i++) {
		funPinMode( led_lines[i], GPIO_CFGLR_IN_FLOAT );
	}
}

void blink_framebuffer() {
	for(int i = 0; i < FB_NCOL; i++) {
		blink_2col(0, i, framebuffer[i]);
	}
}


__INTERRUPT
void GPIOA_IRQHandler() {
	int status = R16_PA_INT_IF;
	R16_PA_INT_IF = status; // acknowledge

	if(status & PIN_KEY1_INT) {
		wakeup_source = IRQ_KEY1;
	}

	if(status & PIN_CHARGE_STT_INT) {
		wakeup_source = IRQ_CHARGE_STT;
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
void RTC_IRQHandler() {
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
	funPinMode( PIN_CHARGE_STT, GPIO_CFGLR_IN_PU ); // Set PIN_CHARGE_STT to input, pullup as connecting charger makes this go to gnd

	// key1 and charge stt interrupts
	R16_PA_INT_MODE |= (PIN_KEY1_INT | PIN_CHARGE_STT_INT); // edge mode, should go to ch32fun.h
	funDigitalWrite(PIN_KEY1, FUN_HIGH); // rising edge
	funDigitalWrite(PIN_CHARGE_STT, FUN_LOW); // falling edge

	NVIC_EnableIRQ(GPIOA_IRQn);
	R16_PA_INT_IF = (PIN_KEY1_INT | PIN_CHARGE_STT_INT); // reset key1 and charge stt flags
	R16_PA_INT_EN |= (PIN_KEY1_INT | PIN_CHARGE_STT_INT); // enable key1 and charge stt interrupts

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

uint8_t battery_percent() {
	// just linear between 100% and 0%
	int charge_available = gs_vbat_mV - VBAT_0;
	int percent = (charge_available * 100) / (VBAT_100 - VBAT_0);
	percent = (percent < 0) ? 0:
			(percent > 100) ? 100:
							percent;
	return (uint8_t)percent;
}

void advertise_BLE() {
	uint32_t MAC = *(vu32*)ROM_CFG_MAC_ADDR;
	adv[4] = (uint8_t)(MAC >> 24);
	adv[5] = (uint8_t)(MAC >> 16);
	adv[6] = (uint8_t)(MAC >> 8);
	adv[7] = (uint8_t)MAC;
	MAC = *(vu32*)(ROM_CFG_MAC_ADDR +4);
	adv[2] = (uint8_t)(MAC >> 8);
	adv[3] = (uint8_t)MAC;
	adv[12] = battery_percent();

	for(int c = 0; c < sizeof(adv_channels); c++) {
		Frame_TX(ACCESS_ADDRESS, adv, sizeof(adv), adv_channels[c], PHY_1M);
	}
}

static volatile uint32_t wakeups;
void scheduled_wakeup_task() {
	wakeups++;

	update_battery_voltage_mV();
	advertise_BLE();

	if(wakeups % 10 == 0) {
		blink_single(1, 0,0);
		blink_single(1, 43,0);
		blink_single(1, 43,10);
		blink_single(1, 0,10);
		blink_2col(1, 1, 0b11111111111); // x=2
		blink_2col(1, 20, (0b11111111111 << MATRIX_NROW)); // x=41
	}
}

void key1_pressed() {
	blink_2col(1, 21, 0b11 << MATRIX_NROW); // x=43
	for(int i = 0; i < FB_NCOL; i++) {
		framebuffer[i] = 0xFFFFFFFF;
	}
	for(int s = 0; s < 100; s++) {
		blink_framebuffer();
	}
}

void key2_pressed() {
	blink_2col(1, 21, 0b110000 << MATRIX_NROW); // x=43
	if(IS_CHARGING) {
		DCDCEnable(); // this seems to be needed!?
		jump_isprom();
	}
}

void charger_connected() {
	blink_2col(1, 21, 0b11110000000 << MATRIX_NROW); // x=43
}


int main() {
	SystemInit();

	// GPIO setup
	funGpioInitAll(); // no-op on ch5xx
	GPIOSetup();

	// Sleep setup
	LSIEnable(); // Disable LSE, enable LSI
	RTCInit(); // Set the RTC counter to 0 and enable RTC Trigger
	SleepInit(); // Enable wakeup from sleep by RTC, and enable RTC IRQ

	// RF setup
	uint8_t txPower = LL_TX_POWER_0_DBM;
	RFCoreInit(txPower);

	// Some one-off startup stuff
	blink_single(1, 23,5); // middle

	while(1) {
		switch(wakeup_source) {
		case IRQ_RTC:
			scheduled_wakeup_task();
			break;
		case IRQ_KEY1:
			key1_pressed();
			break;
		case IRQ_KEY2:
			key2_pressed();
			break;
		case IRQ_CHARGE_STT:
			charger_connected();
			break;
		case IRQ_NONE: // fall-through
		default:
			break;
		}

		// closing matters
		wakeup_source = IRQ_NONE;
		if(IS_CHARGING) {
			for(int i = 0; i < FB_NCOL; i++) {
				framebuffer[i] = i;
			}
			blink_framebuffer();
		}
		else {
			LowPower( MS_TO_RTC(WAKEUP_INTERVAL_MS), (RB_PWR_RAM2K | RB_PWR_RAMX | RB_PWR_EXTEND) );
			RFCoreInit(txPower); // RF wakes up in an odd state, we need to reinit after sleep
		}
	}
}
