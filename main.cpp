#include <stdint.h>
#include "Framebuffer.h"
#include "images.h"
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#define A_B  7
#define BUF  6
#define GA  5
#define SHDN  4
#define CS1 PINB2
#define CS2 PINB1
#define SCL_CLOCK  100000L

#define SCAN0 6
#define SCAN1 7
#define ROW0 2
#define ROW1 3
#define ROW2 4
#define ROW3 5
#define STR_STP_BTN 2
#define FUNC_BTN 3

uint32_t tickCount = 1960;
uint32_t tickCurrent = 0;
uint32_t tickStop = 1960;

float Vref = 5;//change according to Vref;

struct ChordCell
{ 
	uint8_t note4;//highest
	uint8_t note3;
	uint8_t note2;
	uint8_t note1;//lowest
	uint8_t gate;//length of gate signal (percent: 100% for tie)
};
ChordCell cells[8];
ChordCell cellSet;

uint8_t progression[16];
uint8_t progression_new[16];
uint8_t progression_new_curr_step = 0;
uint8_t currentCell;
uint8_t progression_currentCell = 0;
uint8_t setcurrentCell = 9;
uint8_t currentCellPos = 0;
uint8_t set_currcell_note_selector = 0;
uint8_t button_flag = 0xFF;
uint8_t settings_flag = 0;
uint8_t current_sett_win = 0;
uint8_t settings_selector = 0;
uint8_t noteLengthDivision = 1; //1,2 or 4
uint8_t bpm = 120;
uint8_t devision_set [5] = {1,2,4,6,8};
uint8_t current_choice = 1;
uint8_t last_choice = 1;
uint8_t _setbpm = 120;
uint8_t buttons = 0b00000000;
uint8_t buttons_special = 0;
uint8_t isPlaying = 0;
Framebuffer fb;


void SPI_MasterInit(void);
unsigned char SPI_MasterTransmit(char outData);
unsigned int MCP4922_Convert_Data (float X, float Vref);
void MCP4922_Sent_Data(int CS, int aOrb, unsigned int h);//cs: for 1 = PINB1; for 2 = PINB2; aorb: dac1 = 0; dac2 = 1;
void initTimer();
void setNewTick(int bpm,int div);
float midiToVoltOct(uint8_t midiCode);
void initProgression();
void formVoltages(ChordCell cell);
void sendToDiodes(uint8_t message);
uint8_t button_state(uint8_t bit);
uint8_t readMatrix();
void draw_number(int number,uint8_t x, uint8_t y);
void draw_chord(uint8_t id);
void draw_main();
void draw_settings();
void draw_settings_selector(uint8_t pos);
void draw_set_time();
void draw_set_gate();
void draw_set_seq();
void draw_set_chord();
void matrix_process_main(uint8_t i);
void matrix_process_settmain(uint8_t i);
void matrix_process_time(uint8_t i);
void matrix_process_gate(uint8_t i);
void matrix_process_seq(uint8_t i);
void matrix_process_chord(uint8_t i);
void func_process_main();
void func_process_settmain();
void func_process_time();
void func_process_gate();
void func_process_seq();
void func_process_chord();
void flag_processer();
void revert_settings();
void copy_arrays();
int main(void) 
{
	DDRD |= (1<<SCAN0)|(1<<SCAN1)|(0<<ROW0)|(0<<ROW1)|(0<<ROW2)|(0<<ROW3);//init matrix
	
	//DDRD = 0xFF;
	DDRC = 0xFF;
	DDRC |= (0<<STR_STP_BTN)|(0<<FUNC_BTN);//init function keys4
	PORTC |= (1<<STR_STP_BTN)|(1<<FUNC_BTN);//for inverted logic
	
	//init phase

	initProgression();
	currentCell = progression[0];
	noteLengthDivision = 2;//each time division is changed setNewTIck must be called
	setNewTick(bpm,noteLengthDivision);//after currentCell is set
	initTimer();
	SPI_MasterInit();
	draw_main();
	//end
	formVoltages(cells[0]);
	
	
	_delay_ms(50);
	while(1)
	{
		buttons = readMatrix();
		//buttons_special = (button_state(STR_STP_BTN)<<0)|(button_state(FUNC_BTN)<<1);
		if(!(button_flag & (1<<0))&&(button_state(STR_STP_BTN)<<0))
		{
			if(settings_flag == 0)
			{
				button_flag |= (1<<0);
				isPlaying ^= 1;
				PORTB|=(1<<0);
				tickCurrent = 0;
				currentCellPos = progression[0];
				draw_chord(currentCellPos);
				sendToDiodes((1<<(currentCellPos%8)));
				tickStop = (tickCount/100.0)*cells[currentCellPos].gate;
			}
			else
			{
				draw_main();
				revert_settings();
				settings_flag = 0;
			}
		}
		if(!(button_flag & (1<<1))&&(button_state(FUNC_BTN)<<1))
		{
			button_flag |= (1<<1);
			if(settings_flag == 0)
			{
				func_process_main();
			}
			else
			{
				switch(current_sett_win) {
					case 0 : func_process_settmain();
					break;
					case 1 : func_process_time();
					break;
					case 2 : func_process_gate();
					break;
					case 3 : func_process_seq();
					break;
					case 4 : func_process_chord();
				}
			}
			
		}
		if(buttons!=0x00)
		{
			
			for (uint8_t i=0;i<8;i++)
			{
				if(buttons & (1<<i))
				{
					if(settings_flag==0)
					{
						matrix_process_main(i);
					}
					if (!(button_flag & (1<<2))&&settings_flag==1)
					{
						
						button_flag |= (1<<2);
						switch(current_sett_win) {
							case 0 : matrix_process_settmain(i);
							break;
							case 1 : matrix_process_time(i);
							break;
							case 2 : matrix_process_gate(i);
							break;
							case 3 : matrix_process_seq(i);
							break;
							case 4 : matrix_process_chord(i);
						}
						//_delay_ms(100);
					}
					break;
				}
			}
		}
		else
		{
			if(isPlaying == 0){
				PORTB&=~(1<<0);//turn down since we dont pressing any button and seq isnt running
			}
			sendToDiodes(0);
		}	
		flag_processer();
		//_delay_ms(10);
	}
	return 0;
}

/*	switch(//value) {
		case 0 : foo();
		break;
		case 1 : ffoo();
		break;
		case 2 : fffoo();
		break;
		case 3 : ffffoo();
		break;
		default: bar();
	}
*/
void matrix_process_main(uint8_t i)
{
	sendToDiodes((1<<i));
	currentCell = i;
	draw_chord(currentCell);
	PORTB|=(1<<0);
	formVoltages(cells[currentCell]);
}
void matrix_process_settmain(uint8_t i)
{
	switch(i) {
		case 0 : settings_selector++;
		break;
		case 1 : settings_selector--;
		break;
	}
	if(settings_selector>4) {settings_selector = 1;}
	if(settings_selector==0) {settings_selector = 4;}
	draw_settings_selector(settings_selector);
}
void matrix_process_time(uint8_t i)
{
	switch(i) {
		case 0 : _setbpm--;
		break;
		case 1 : _setbpm++;
		break;
		case 2 : current_choice--;
		break;
		case 3 : current_choice++;
	}
	if(current_choice>4) { current_choice = 4; }
	draw_number(_setbpm,38,22);
	draw_number(devision_set[current_choice],71,37);
	fb.show();
}
void matrix_process_gate(uint8_t i)
{
	if (setcurrentCell == 9)
	{
		currentCell = i;
		draw_number(currentCell,66,26);
		draw_number(cells[currentCell].gate,55,41);//add accept chord and stuff
	}
	else
	{
		switch(i) {
			case 0 : cellSet.gate++;
			break;
			case 1 : cellSet.gate--;
			break;
		}
		draw_number(cellSet.gate,55,41);
	}
	fb.show();
}
void matrix_process_seq(uint8_t i)
{
	currentCell = i;
	progression_new_curr_step++;
	progression_new[progression_new_curr_step] = currentCell;
	if(progression_new_curr_step>16) func_process_seq();
	draw_number(progression_new_curr_step,56,41);
	fb.show();
}
void matrix_process_chord(uint8_t i)
{
	if (setcurrentCell == 9)
	{
		currentCell = i;
		draw_number(currentCell,66,26);
		//draw_number(cells[currentCell].gate,55,41);//add accept chord and stuff
		fb.show();
	}
	else
	{
		switch(i) {
			case 0 : cellSet.note1--;
			break;
			case 1 : cellSet.note1++;
			break;
			case 2 : cellSet.note2--;
			break;
			case 3 : cellSet.note2++;
			break;
			case 4 : cellSet.note3--;
			break;
			case 5 : cellSet.note3++;
			break;
			case 6 : cellSet.note4--;
			break;
			case 7 : cellSet.note4++;
		}
		draw_number(cellSet.note1,2,46);
		draw_number(cellSet.note2,34,46);
		draw_number(cellSet.note3,66,46);
		draw_number(cellSet.note4,98,46);
		fb.show();
	}
}
void func_process_main()
{
	settings_flag = 1;
	PORTB&=~(1<<0);
	draw_settings();
}
void func_process_settmain()
{
	if (settings_selector<=4&&settings_selector!=0)
	{
		current_sett_win=settings_selector;
		
	}
	switch(settings_selector) {
		case 1 : draw_set_time();
		break;
		case 2 : draw_set_gate();
		break;
		case 3 : draw_set_seq();
		break;
		case 4 : draw_set_chord();
	}
}
void func_process_time()
{
	draw_main();
	bpm = _setbpm;
	noteLengthDivision = devision_set[current_choice];
	last_choice = current_choice;
	setNewTick(bpm,noteLengthDivision);
	settings_flag = 0;
	current_sett_win = 0;
}
void func_process_gate()
{
	if (setcurrentCell == 9)
	{
		setcurrentCell = currentCell;
		cellSet = cells[setcurrentCell];
	}
	else
	{
		draw_main();
		settings_flag = 0;
		current_sett_win = 0;
		setcurrentCell = 9;
		cells[currentCell] = cellSet;
	}
}
void func_process_seq()
{
	if(progression_new_curr_step>16)
	{
		copy_arrays();
		//progression = progression_new;
		progression_new_curr_step = 0;
		draw_main();
		settings_flag = 0;
		current_sett_win = 0;
	}
}
void func_process_chord()
{
	if (setcurrentCell == 9)
	{
		setcurrentCell = currentCell;
		cellSet = cells[setcurrentCell];
		draw_number(cellSet.note1,2,46);
		draw_number(cellSet.note2,34,46);
		draw_number(cellSet.note3,66,46);
		draw_number(cellSet.note4,98,46);
		fb.show();
	}
	else
	{
		draw_main();
		setcurrentCell = 9;
		settings_flag = 0;
		current_sett_win = 0;
		cells[currentCell] = cellSet;
	}
	//add result part
}
void draw_main(){
	fb.clear();
	fb.drawBitmap(start,34,128,0,0);
	draw_chord(currentCell);
	fb.show();
}
void draw_settings()
{
	fb.clear();
	fb.drawBitmap(setts[0],64,128,0,0);
	settings_selector = 1;
	draw_settings_selector(settings_selector);
	fb.show();
}
void draw_settings_selector(uint8_t pos)
{
	pos--;
	fb.drawBitmap(blank,8,8,3,12);
	fb.drawBitmap(blank,8,8,3,23);
	fb.drawBitmap(blank,8,8,3,34);
	fb.drawBitmap(blank,8,8,3,45);
	switch(pos) {
		case 0 : fb.drawBitmap(arrow,8,8,3,12);
		break;
		case 1 : fb.drawBitmap(arrow,8,8,3,23);
		break;
		case 2 : fb.drawBitmap(arrow,8,8,3,34);
		break;
		case 3 : fb.drawBitmap(arrow,8,8,3,45);
		break;
		default: fb.drawBitmap(arrow,8,8,16,16);
	}
	fb.show();
}
void draw_chord(uint8_t id)
{
	draw_number(id,75,19);
	draw_number(cells[id].note1,2,46);
	draw_number(cells[id].note2,34,46);
	draw_number(cells[id].note3,66,46);
	draw_number(cells[id].note4,98,46);
	fb.show();
}
void draw_number(int number, uint8_t x, uint8_t y)
{
	uint8_t a[3] = {0,0,0};
	uint8_t shift=0,i=0;
    while(number)
    {
	    int curr = number % 10;
	    number /= 10;
		a[i] = curr;
		
		i++;
	}
	for(i = 3;i!=0;i--)
	{
		fb.drawBitmap(nums[a[i-1]],8,8,x+shift,y);
		shift+=10;
	}
	//fb.show();
}
void draw_set_time()
{
	fb.clear();
	fb.drawBitmap(setts[current_sett_win],64,128,0,0);
	draw_number(bpm,38,22);
	draw_number(devision_set[current_choice],71,37);
	fb.show();
}
void draw_set_gate()
{
	fb.clear();
	fb.drawBitmap(setts[current_sett_win],64,128,0,0);
	draw_number(currentCell,66,26);
	draw_number(cells[currentCell].gate,55,41);
	fb.show();
}
void draw_set_seq()
{
	fb.clear();
	fb.drawBitmap(setts[current_sett_win],64,128,0,0);
	draw_number(16,89,41);
	fb.show();
}
void draw_set_chord()
{
	fb.clear();
	fb.drawBitmap(setts[current_sett_win],64,128,0,0);
	fb.show();
}
void revert_settings()
{
	current_sett_win = 0;
	_setbpm = bpm;
	current_choice = last_choice;
	setcurrentCell = 9;
	progression_new_curr_step = 0;
}
void flag_processer()
{
	if(!button_state(STR_STP_BTN))
	{
		button_flag &= ~(1<<0);
	}
	if(!button_state(FUNC_BTN))
	{
		button_flag &= ~(1<<1);
	}
	if(!readMatrix())
	{
		button_flag &= ~(1<<2);
	}
	buttons = 0b00000000;
}
void initTimer()
{
	//DDRD = 0b01001000;
	//DDRC = 0xFF;
	//DDRB = (1<<0)
	TCNT0 = 0x00;
	OCR0A = 127;
	TIMSK0 = (1<<TOIE0)|(1<<OCIE0A);
	TCCR0A|=(1<<WGM00)|(0<<WGM01)|(0<<COM0A1)|(0<<COM0A0);
	TCCR0B|=(0<<CS00)|(1<<CS01);
	sei();
}
ISR(TIMER0_OVF_vect)
{
	//putt if for start stop button
	if (isPlaying&&!settings_flag)
	{
		tickCurrent++;
		if (tickCurrent == tickStop)
		{
			PORTB&=~(1<<0);
		}
		if (tickCurrent==tickCount)
		{
			
			//managing ticks
			tickCurrent = 0;
			currentCellPos++;
			if(currentCellPos==16){
				currentCellPos = 0;
			}
			//applying step
			progression_currentCell = progression[currentCellPos];
			tickStop = (tickCount/100.0)*cells[progression_currentCell].gate;
			if(buttons==0x00)
			{
				PORTB|=(1<<0);//to stop gate from not happenning while button si pressed
				draw_chord(progression_currentCell);
				formVoltages(cells[progression_currentCell]);
				sendToDiodes((1<<(currentCellPos%8)));
			}
		}
	}
}
ISR(TIMER0_COMPA_vect)
{
	//PORTD&=~(1<<3);
}
 uint8_t button_state(uint8_t bit)
 {
	 if(!(PINC & (1<<bit)))
	 {
		 _delay_ms(10);
		 if(!(PINC & (1<<bit))) return 1;
	 }
	 return 0;
 }
uint8_t readMatrix()
{
	uint8_t result = 0b00000000, buff=0x00;
	PORTD |= (1<<SCAN1);//столбец 1 высокое напряжение
	_delay_ms(1);
	result |= PIND & ((1<<(ROW0))|(1<<(ROW1))|(1<<(ROW2))|(1<<(ROW3)));//чтение строчек
	
	PORTD &= (0<<SCAN1);
	result = (result<<2);
	
	PORTD |= (1<<SCAN0);//столбец 2 высокое напряжение
	_delay_ms(1);
	buff |= PIND & ((1<<(ROW0))|(1<<(ROW1))|(1<<(ROW2))|(1<<(ROW3)));//чтение строчек
	PORTD &= (0<<SCAN0);
	result |= (buff>>2);
	return result;
}
void sendToDiodes(uint8_t message)
{
	//unsigned int   mask_inf = message>>8;
	//mask_inf = 0b00000000;
	//PORTD = mask_inf;
	PORTC &=~(1<<0);
	//SPI_MasterTransmit (mask_inf); //отправляем старший байт
	SPI_MasterTransmit (message); //отправляем младший байт
	PORTC |=(1<<0);
}
void setNewTick(int bpm,int div)
{
	tickCurrent = 0;
	tickCount = (235294/bpm)*((1.0/div)*4);
	tickStop = (tickCount/100.0)*cells[currentCell].gate;
}
void SPI_MasterInit(void)
{
	// Установка выводов SPI на вывод
	DDRB = 0xFF;
	//Включение SPI, режима ведущего, и установка частоты тактирования fclk/128
	SPCR = (1<<SPE)|(1<<MSTR)|(1<<SPR1)|(1<<SPR0);
}
//returns voltage of passed midi note. For note values look the table.
float midiToVoltOct(uint8_t midiCode)
{
	return (1.0/12.0)*(float(midiCode) - 24);
}
/* Функция передачи байта данных outData. Ожидает окончания
передачи и возвращает принятый по ножке MOSI байт */
unsigned char SPI_MasterTransmit(char outData)
{
	// Начало передачи
	SPDR = outData;
	// Ожидание окончания передачи
	while(!(SPSR & (1<<SPIF))) ;
	return SPDR; //возвращаем принятый байт
}

unsigned int MCP4922_Convert_Data (float X, float Vref)
{
	// Dout=(Vout*4096)/Vref
	unsigned int   u;
	
	X=X/Vref;
	u =(unsigned int)((X*4096));
	return u;
}

void MCP4922_Sent_Data(int CS, int aOrb, unsigned int h)//cs: for 1 = PINB1; for 2 = PINB2; aorb: dac1 = 0; dac2 = 1;
{
	unsigned int   mask_inf = h>>8;
	mask_inf |=(1<<SHDN)|(1<<GA)|(aOrb<<A_B);
	//PORTD = mask_inf;
	PORTB &=~(1<<CS);
	SPI_MasterTransmit (mask_inf); //отправляем старший байт
	SPI_MasterTransmit (h); //отправляем младший байт
	PORTB |=(1<<CS);
}
void initProgression()
{
	ChordCell buff;
	/*buff.note4 = 60;
	buff.note3 = 64;
	buff.note2 = 67;
	buff.note1 = 71;*/
	buff.gate = 50;
	for (int i = 0; i < 8;i++)
	{
		//buff.gate = 15*(i+1);
		buff.note4 = 36+i*4;
		buff.note3 = 40+i*4;
		buff.note2 = 43+i*4;
		buff.note1 = 48+i*4;
		cells[i] = buff;
	}
	cellSet = buff;
	for (int i = 0; i < 16;i++)
	{
		progression[i] = i%8;
		progression_new[i] = progression[i];
	}
}
void formVoltages(ChordCell cell)
{
	MCP4922_Sent_Data(1,0, MCP4922_Convert_Data (midiToVoltOct(cell.note4),Vref));
	MCP4922_Sent_Data(1,1, MCP4922_Convert_Data (midiToVoltOct(cell.note3),Vref));
	MCP4922_Sent_Data(2,0, MCP4922_Convert_Data (midiToVoltOct(cell.note2),Vref));
	MCP4922_Sent_Data(2,1, MCP4922_Convert_Data (midiToVoltOct(cell.note1),Vref));
}
void copy_arrays()
{
	for (int i = 0; i<16;i++)
	{
		progression[i] = progression_new[i];
	}
}