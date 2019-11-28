/*
 * PART 4
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "system.h"
#include "sys/alt_alarm.h"
#include "io.h"
#include "sys/alt_irq.h"

#include "fatfs.h"
#include "diskio.h"

#include "ff.h"
#include "monitor.h"
#include "uart.h"

#include "alt_types.h"
#include "altera_up_avalon_audio.h"
#include "altera_up_avalon_audio_and_video_config.h"

#include "altera_avalon_pio_regs.h"
#include "altera_avalon_timer_regs.h"
#include "altera_avalon_timer.h"

#define ESC 27
#define CLEAR_LCD_STRING "[2J"

typedef enum TrackState {
	STOP, PAUSE, BACK, NEXT, PLAY
} TrackState;
typedef enum TrackPlayback {
	NORMAL, DOUBLE, HALF, MONO
} TrackPlayback;

volatile static int count = 0;
volatile static int buttonState = 0;
volatile TrackState currState = STOP;
volatile TrackState prevState = STOP;
volatile TrackPlayback playBackMode = NORMAL;
volatile int nextSongFlag = 0;

char *ptr, *ptr2;
char *ptr, *ptr2;

char *fileNames[20]; //name of files
unsigned long fileSizes[20]; //size of files
int songIndex = 0;
int total_files = 0;
long p1, p2, p3;

unsigned int l_buf;
unsigned int r_buf;

int previous = 0;
int pushButton = 0;

FRESULT res;
FATFS Fatfs[1];
FIL File1;
uint8_t *Buff;
uint32_t cnt;
FILE* lcd;
alt_up_audio_dev *audio_dev;

void toggle_LED(int val) {
	int curr = IORD(LED_PIO_BASE, 0);
	curr ^= val;
	IOWR(LED_PIO_BASE, 0, curr);
}
static void timer_ISR(void* context, alt_u32 id) {
	previous = pushButton;
	pushButton = IORD(BUTTON_PIO_BASE, 0);
	IOWR(TIMER_0_BASE, 0, 0); //stop the timer ITO
	IOWR(TIMER_0_BASE, 1, 0); //disabling timer interrupt

	IOWR(TIMER_0_BASE, 1, 9); //stop the timer ITO
	IOWR(BUTTON_PIO_BASE, 3, 0); //resetting edge capture
	IOWR(BUTTON_PIO_BASE, 2, 0xF); // F=1111 writing interrupt to all push buttons
//	 toggle_LED(4);

}
static void button_pressed_ISR(void* context, alt_u32 id) {
	IOWR(BUTTON_PIO_BASE, 3, 0); //resetting edge capture
	IOWR(BUTTON_PIO_BASE, 2, 0); //disabling button interrupt
	IOWR(TIMER_0_BASE, 1, 9); //stop the timer ITO
	IOWR(TIMER_0_BASE, 1, 5); //offset of 1 = control, 5 corresponds to
							  // Start=1 --> writing 1 to start bit starts the internal counter running (count down)
							  // Count=0 --> if COUNT=0, the counter stops after it reaches zero
							  // TO=1 --> the interval timer core generates an IRQ when the status register's TO bit is 1

//	toggle_LED(2);
}

int displayLCD(char * mode) {
	lcd = fopen("/dev/lcd_display", "w");
	if (lcd != NULL) {
		fprintf(lcd, "\r%d %s\n",songIndex+1, fileNames[songIndex]);
		fprintf(lcd, "%s\n", mode);
	}
	fclose(lcd);
	return 0;
}
void clearLCD(){
	lcd = fopen("/dev/lcd_display", "w");
	if (lcd != NULL) {
		fprintf(lcd, "%c%s", ESC, CLEAR_LCD_STRING);
	}
	fclose(lcd);
}
void updateLCDText() {
	clearLCD();
	switch (currState) {
	case STOP:
		displayLCD("STOPPED");
		break;
	case PAUSE:
			displayLCD("PAUSED");
			break;
	case BACK:

		if(songIndex <=0){
			songIndex=total_files-1;
		}else{
			songIndex--;
		}
		displayLCD("STOPPED");
		if(prevState == PLAY ){
			prevState=currState;
			currState=PLAY;
			pressedPlay();
			playTrack();

			}else if (prevState == PAUSE){
				prevState=currState;
				currState=STOP;
				displayLCD("STOPPED");
			}

			break;

	case NEXT:

		if((songIndex) >= total_files-1){
			songIndex=0;
		}else{
			songIndex++;
		}
		displayLCD("STOPPED");
		if(prevState == PLAY ){
		prevState=currState;
		currState=PLAY;
		pressedPlay();
		playTrack();

		}else if (prevState == PAUSE){
			prevState=currState;
			currState=STOP;
			displayLCD("STOPPED");
		}
		break;
	case PLAY:
		pressedPlay();
		if(prevState!= PAUSE){
			playTrack();
		}

		break;

	}

}
void pressedPlay(){
	checkSwitches();
		if (playBackMode == HALF) {
			displayLCD("PLAY-ST-HALF");
		} else if (playBackMode == DOUBLE) {
			displayLCD("PLAY-ST-DBL");
		} else if (playBackMode == NORMAL) {
			displayLCD("PLAY-ST-NORMAL");
		} else if (playBackMode == MONO) {
			displayLCD("PLAY-ST-MONO");
		}
}
void playTrack() {
	int displayed=0;
	alt_up_audio_reset_audio_core(audio_dev);
	p1 = fileSizes[songIndex];

	res = f_open(&File1, fileNames[songIndex], (uint8_t) 1);

	if (res != FR_OK) {
		return;
	}

	Buff = malloc(sizeof(uint8_t) * p1);
	f_lseek(&File1, 44);
	p1 = p1 + 44;
	res = f_read(&File1, Buff, p1, &(p1));

	if (res != FR_OK) {
		return;
	}
	unsigned int wordIndex = 0;
	unsigned int bytesExpected = 512;

	if (playBackMode == NORMAL) {
		while (wordIndex < fileSizes[songIndex]) {
			l_buf = (Buff[wordIndex + 1] << 8) | Buff[wordIndex]; // shift to upper 8 bits
			r_buf = (Buff[wordIndex + 3] << 8) | Buff[wordIndex + 2]; //or to add other 8 bits
			while (alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_LEFT)< 1);
			alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT); //push to fifo queue
			alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
			checkButtons();
			while(currState==PAUSE){
				checkButtons();
			} if(currState == STOP){
				break;
			}
			wordIndex += 4;
		}



	}
	if (playBackMode == HALF) {
		while (wordIndex < fileSizes[songIndex]) {
			l_buf = (Buff[wordIndex + 1] << 8) | Buff[wordIndex]; // shift to upper 8 bits
			r_buf = (Buff[wordIndex + 3] << 8) | Buff[wordIndex + 2]; //or to add other 8 bits
			while (alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_LEFT)< 1);
			alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT); //push to fifo queue
			alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
			checkButtons();
			while(currState==PAUSE){
				checkButtons();
			} if(currState == STOP){
				break;
			}
			wordIndex += 2;
		}
	}
	if (playBackMode == DOUBLE) {
		while (wordIndex < fileSizes[songIndex]) {
			l_buf = (Buff[wordIndex + 1] << 8) | Buff[wordIndex]; // shift to upper 8 bits
			r_buf = (Buff[wordIndex + 3] << 8) | Buff[wordIndex + 2]; //or to add other 8 bits
			while (alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_LEFT)< 1);
			alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT); //push to fifo queue
			alt_up_audio_write_fifo(audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
			checkButtons();
			while(currState==PAUSE){
				checkButtons();
			} if(currState == STOP){
				break;
			}
			wordIndex += 8;
	}}
	if (playBackMode == MONO) {
		while (wordIndex < fileSizes[songIndex]) {
			// right audio to both channels
			r_buf = (Buff[wordIndex + 1] << 8) | Buff[wordIndex + 2];
			while (alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_LEFT)< 1);
			alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
			alt_up_audio_write_fifo(audio_dev, &(r_buf), 1, ALT_UP_AUDIO_LEFT);
			checkButtons();
			while(currState==PAUSE){
				checkButtons();
			} if(currState == STOP){
				break;
			}
			wordIndex += 4;

		}
	}
	free(Buff);
	printf("done\n");
	if(currState != PLAY){
		updateLCDText();
		return;
	}else{
		currState = STOP;
		updateLCDText();
		return;
	}



}

int isWav(char * file) {
	int len = strlen(file);
	if (len < 4) {
		return 0;
	}
	int a = !strcmp(".wav", file + (len - 4));
	int b = !strcmp(".WAV", file + (len - 4));
	return a || b;
}
void checkButtons() {

	//check value of buttons
	if (pushButton == 0x0E) {
//		IOWR(LED_PIO_BASE, 0, pushButton);
		pushButton = 0;
		previous = 0;
		prevState= currState;
		currState = NEXT;
		updateLCDText();

	} else if (pushButton == 0x0D) {
//		IOWR(LED_PIO_BASE, 0, pushButton);
		pushButton = 0;
		previous = 0;

		if (currState == PLAY) {
			prevState= currState;
			currState = PAUSE;
		} else {
			prevState= currState;
			currState = PLAY;
		}
		updateLCDText();

	} else if (pushButton == 0x0B) {
//		IOWR(LED_PIO_BASE, 0, pushButton);
		pushButton = 0;
		previous = 0;
		prevState= currState;
		currState = STOP;
		updateLCDText();

	} else if (pushButton == 0x07) {
//		IOWR(LED_PIO_BASE, 0, pushButton);
		pushButton = 0;
		previous = 0;
		prevState= currState;
		currState = BACK;
		updateLCDText();
	}

}
void checkSwitches() {

	int sw0 = IORD(SWITCH_PIO_BASE, 0) & 1; //since its active low and it with 1
	int sw1 = (IORD(SWITCH_PIO_BASE, 0) >> 1) & 1; // since we need second bit, shift right and & with 1 (b/c active low)
//	xprintf("sw0: %d; sw1: %d\n", sw0, sw1);
	if (!sw0 && !sw1) {
		playBackMode = NORMAL;
	} else if (sw0 && !sw1) {
		playBackMode = HALF;
	} else if (!sw0 && sw1) {
		playBackMode = DOUBLE;
	} else {
		playBackMode = MONO;
	}
}

int addFiles() {
	FILINFO Finfo;
	DIR Dir;
	char *ptr;
	long p1;
	uint8_t res = 0;
	FATFS *fs; /* Pointer to file system object */
	//	int counter = 0;
	ptr = NULL;

	res = f_opendir(&Dir, "");
	if (res) // if res in non-zero there is an error; print the error.
	{
		return -1;
	}
	p1 = 0; // otherwise initialize the pointers and proceed.

	while (1) {
		res = f_readdir(&Dir, &Finfo);
		if ((res != FR_OK) || !Finfo.fname[0])
			break;
		else {
			p1 += Finfo.fsize;
		}
		if (isWav(&(Finfo.fname[0]))) {
			fileNames[total_files] = malloc(256);
			strcpy(fileNames[total_files], &(Finfo.fname[0]));
			fileSizes[total_files] = Finfo.fsize;

			total_files++;
		}
	}

	f_getfree(ptr, (uint32_t *) &p1, &fs);

	return 0;

}
int main() {

	IOWR(BUTTON_PIO_BASE, 2, 0xF); // turn on button IRQ
	IOWR(LED_PIO_BASE, 0, 0); // clear LED's
	alt_irq_register(BUTTON_PIO_IRQ, (void*) 0, button_pressed_ISR); // register button ISR
	alt_irq_register(TIMER_0_IRQ, (void*) 0, timer_ISR); // register timer ISR
	IOWR(TIMER_0_BASE, 3, 0x13); // set upper end of Period
	IOWR(TIMER_0_BASE, 2, 0x12D0); // set lower end of Period
	IOWR(BUTTON_PIO_BASE, 3, 0); //resetting edge capture
	IOWR(BUTTON_PIO_BASE, 2, 0xF); // F=1111 writing interrupt to all push buttons

	//open audio port

	audio_dev = alt_up_audio_open_dev("/dev/Audio");

	if (audio_dev == NULL) {

		alt_printf("Error: could not open audio device \n");
	} else {
		alt_printf("Opened audio device \n");
	}

	res = disk_initialize(0);
	if (res != FR_OK) {
		return -1;
	}
	res = f_mount(0, &Fatfs[0]);
	if (res != FR_OK) {
		return -1;
	}

	addFiles();
	updateLCDText();
	while (1) {
		checkButtons();
	}
	return 0;
}

