/*
 * attiny2313_led.c
 *
 * Created: 2015/10/21 9:16:16
 * Author : zenmai
 */ 

#ifndef F_CPU
#define F_CPU 8000000UL // 8 MHz clock speed
#endif

#include <stdlib.h>
#include <avr/io.h>
#include <util/delay.h>

#define sdCsDeSelect() (PORTB|=(1<<PB4))
#define sdCsSelect() (PORTB&=~(1<<PB4))
uint8_t secPerCls;
union uniUint16_t apuPc;
uint8_t apu00,apu01,apuA,apuX,apuY,apuPsw,apuSp,apuInitCode[0x16+12];
uint16_t playTime;

union uniUint32_t{
	uint8_t bytes[4];
	uint16_t words[2];
	uint32_t value;
};

union uniUint16_t{
	uint8_t bytes[2];
	uint16_t value;
};

union uniUint16_t fatPos,dirPos,dataPos,blkPos;
union uniUint32_t arg;

uint8_t usiSend(uint8_t c)
{
	USIDR = c;
	USISR = 1<<USIOIF;
	do 
	{
		USICR = 1<<USIWM0 | 1<<USICS1 | 1<<USICLK | 1<<USITC;
	} while ((USISR & (1<<USIOIF))==0);
	return USIDR;
}


uint8_t sdCmdSend(uint8_t c)
{
	uint8_t crc,i;

	sdCsDeSelect();
	usiSend(0xff);
	sdCsSelect();
	usiSend(0xff);
	usiSend(c|0x40);
	usiSend(arg.bytes[3]);
	usiSend(arg.bytes[2]);
	usiSend(arg.bytes[1]);
	usiSend(arg.bytes[0]);
	switch(c)
	{
		case 0:
			crc = 0x95;
			break;
		case 8:
			crc = 0x87;
			break;
		default:
			crc = 0x01;
	}
	usiSend(crc);
	
	for (i=0;i<10;i++)
	{
		crc = usiSend(0xff);
		if (crc<0x80) break;
	}
	
	return crc;
}

uint8_t sdACmdSend(uint8_t c)
{
	sdCmdSend(55);
	return sdCmdSend(c);
}


void sdInit(void)
{
	int i;
	uint8_t c;

	sdCsDeSelect();
	for(c=0;c<10;c++)
		usiSend(0xff);
	sdCsSelect();
	for(i=0;i<600;i++)
		usiSend(0xff);
	
	arg.bytes[0] = 0;
	arg.bytes[1] = 0;
	arg.bytes[2] = 0;
	arg.bytes[3] = 0;
	sdCmdSend(0);
	
	arg.bytes[0] = 0xaa;
	arg.bytes[1] = 0x01;
	sdCmdSend(8);
	
	usiSend(0xff);
	usiSend(0xff);
	usiSend(0xff);
	usiSend(0xff);

	arg.bytes[0] = 0x00;
	arg.bytes[1] = 0x00;
	//arg.bytes[2] = 0x00;
	for(c=0;c<250;c++)
	{
		sdCmdSend(55);
		arg.bytes[3] = 0x40;
		if(sdCmdSend(41)==0)break;
	}

	arg.bytes[3] = 0;
	sdCmdSend(58);
	
	usiSend(0xff);
	usiSend(0xff);
	usiSend(0xff);
	usiSend(0xff);
	
	sdCsDeSelect();
	usiSend(0xff);
}

void sdWaitNextBlock(void)
{
	uint8_t i;
	
	for(i=0;i<255;i++)
		if(usiSend(0xff)==0xfe)
			break;
}

void sdInitReadMultiBlock(void)
{
	sdCmdSend(18);
	sdWaitNextBlock();
	blkPos.value = 0;
}

void sdStopReadMultiBlock(void)
{
	char i;
	
	sdCmdSend(12);
	for(i=0;i<10;i++)
		usiSend(0xff);
}

uint8_t sdReadOneByteFromMultiBlock(void)
{
	if(blkPos.value++ == 512)
	{
		usiSend(0xff);
		usiSend(0xff);
		sdWaitNextBlock();
		blkPos.value = 1;
	}
	return usiSend(0xff);
}

uint8_t sdSkipNByteFromMultiBlock(uint16_t c)
{
	uint8_t d;

	do 
	{
		d = sdReadOneByteFromMultiBlock();
	} while (--c);
	return d;
}

void argSecToByte(void)
{
	arg.value <<= 9;
}

void clsToSec(void)
{
	uint8_t c;
	
	c = secPerCls;
	do 
	{
		arg.value <<= 1;
	} while (c >>= 1);
}

void fat16CalcAdress(void)
{
	arg.bytes[0] = sdSkipNByteFromMultiBlock(0x0f);
	arg.bytes[1] = sdReadOneByteFromMultiBlock();
	arg.bytes[2] = 0;
	arg.bytes[3] = 0;
	
	arg.value -= 2; // fat size
	clsToSec();
	arg.value += dataPos.value;
}

uint8_t fat16FirstSearchFile(uint8_t n)
{
	uint8_t c,a,fn;
	
	arg.bytes[0] = dirPos.bytes[0];
	arg.bytes[1] = dirPos.bytes[1];
	arg.bytes[2] = 0;
	arg.bytes[3] = 0;
	argSecToByte();
	sdInitReadMultiBlock();
	
	fn = 1;
	c = sdReadOneByteFromMultiBlock();
	while (1)
	{
		if (c==0)
		{
			sdStopReadMultiBlock();
			return 0;
		}
		//fn = (c&0x0f)<<4;
		//fn |= (sdReadOneByteFromMultiBlock()&0x0f);
		sdSkipNByteFromMultiBlock(10);
		a = sdReadOneByteFromMultiBlock();
		if (c!=0xe5 && (a&0x1f)==0x00)
		{
			if(fn==n)
			{
				fat16CalcAdress();
				argSecToByte();
				sdSkipNByteFromMultiBlock(4);
				sdStopReadMultiBlock();
				return 1;
			}
			else
				fn++;
		}
		c = sdSkipNByteFromMultiBlock(0x15); // skip next dir entry
	}
}

void fat16Init(void)
{
	union uniUint16_t w;

	arg.bytes[0] = 0;
	arg.bytes[1] = 0;
	arg.bytes[2] = 0;
	arg.bytes[3] = 0;
	sdInitReadMultiBlock();

	fatPos.bytes[0] = sdSkipNByteFromMultiBlock(0x1c7);
	fatPos.bytes[1] = sdReadOneByteFromMultiBlock();
	sdStopReadMultiBlock();

	arg.bytes[0] = fatPos.bytes[0];
	arg.bytes[1] = fatPos.bytes[1];
	//arg.bytes[2] = 0;
	//arg.bytes[3] = 0;
	argSecToByte();

	sdInitReadMultiBlock();
	sdReadOneByteFromMultiBlock();
	
	sdSkipNByteFromMultiBlock(0xb); //0x00 16bit sector size
	sdReadOneByteFromMultiBlock(); //0x02
	
	secPerCls = sdReadOneByteFromMultiBlock() >> 1;//0x40 -> 0x20
	
	w.bytes[0] = sdReadOneByteFromMultiBlock();
	w.bytes[1] = sdReadOneByteFromMultiBlock();
	fatPos.value += w.value;

	sdReadOneByteFromMultiBlock(); //0x02 
	sdReadOneByteFromMultiBlock(); //0x00 root dir entry count
	sdReadOneByteFromMultiBlock(); //0x02
	
	dirPos.bytes[0] = sdSkipNByteFromMultiBlock(0x04);
	dirPos.bytes[1] = sdReadOneByteFromMultiBlock();
	dirPos.value <<= 1; // fat count 2
	dirPos.value += fatPos.value;
	
	dataPos.value = dirPos.value + 32; //32=dir size

	sdStopReadMultiBlock();
}

void apuReset(void)
{
	PORTB &= 0b11110011;
	_delay_ms(100);
	PORTB |= 0b00001100;
	_delay_ms(100);
}

uint8_t apuRead(uint8_t a)
{
	uint8_t d,i;
	
	DDRA = 0b00000000;
	DDRD = 0b00000000;
	PORTB = (PORTB & 0b11110000) | 0b00000100 | (a & 0x03); // /WR high,/RD low
	for (i=0;i<3;i++){
		d = (PIND & 0b01111101) | (PINA & 0b00000010);
		if(PINA & (1<<PINA0))
		d |= 0b10000000;
	}
	PORTB |= 0b00001000;	// /RD high
	return d;
}

void apuWrite(uint8_t a,uint8_t d)
{
	//PORTB = (PORTB & 0b11110000) | 0b00001100 | (a & 0x03); // /WR high,/RD high
	DDRA = 0b00000011;
	DDRD = 0b01111101;
	PORTB = (PORTB & 0b11110000) | 0b00001000 | (a & 0x03); // /WR low,/RD high
	PORTA = d & 0b00000010;
	if(d & 0b10000000)
		PORTA |= 0b00000001;
	PORTD = (d & 0b01111101)|0b00000010;
	PORTB |= 0b00000100;	// /WR high
}

void apuCopyBlock(uint8_t m,uint16_t a,uint16_t s)
{
	union uniUint16_t c,aa;
	uint8_t d,*pt;
	
	pt = apuInitCode;
	aa.value = a;
	c.value = 0;
	while(apuRead(0)!=0xaa);
	//while(apuRead(1)!=0xbb);
	apuWrite(2,aa.bytes[0]);
	apuWrite(3,aa.bytes[1]);
	apuWrite(1,1);
	apuWrite(0,0xcc);
	while(apuRead(0)!=0xcc);

	do 
	{
		if(m==0)
			d = sdReadOneByteFromMultiBlock();
		else
			d = *pt++;
		apuWrite(1,d);
		apuWrite(0,c.bytes[0]);
		while(apuRead(0)!=c.bytes[0]);
		c.value++;
	} while (--s);
	
	apuWrite(2,0xc9);
	apuWrite(3,0xff);
	apuWrite(1,0);
	d = apuRead(0)+2;
	apuWrite(0,d);
	while(apuRead(0)!=d);
}

void playTimeAdd(uint8_t d)
{
	//uint8_t i;
	uint16_t dww;
	
	dww = (uint16_t)(d & 0x0f)<<8;
	//dww += dww << 1;
//	for(dw=0,i=0;i<10;i++)
//		dw += playTime;
	playTime = (playTime<<3) + playTime + playTime + dww;
}

void makeInitCode(void)
{
	uint8_t *pt/*, d1, d2*/;
	
	apuPc.bytes[0] = sdSkipNByteFromMultiBlock(0x26);
	apuPc.bytes[1] = sdReadOneByteFromMultiBlock();
	apuA = sdReadOneByteFromMultiBlock();
	apuX = sdReadOneByteFromMultiBlock();
	apuY = sdReadOneByteFromMultiBlock();
	apuPsw = sdReadOneByteFromMultiBlock();
	apuSp = sdReadOneByteFromMultiBlock();
	sdSkipNByteFromMultiBlock(0x100-0x2c);
	/*
	playTimeAdd(sdReadOneByteFromMultiBlock());
	if ((d1 = sdReadOneByteFromMultiBlock())!=0)
		playTimeAdd(d1);
	if ((d2 = sdReadOneByteFromMultiBlock())!=0)
		playTimeAdd(d2);
	sdSkipNByteFromMultiBlock(0x100-0xac);
	*/
	apu00 = sdReadOneByteFromMultiBlock();
	apu01 = sdReadOneByteFromMultiBlock();
	
	pt = apuInitCode;
	*pt++ = 0x8f;//0 mov [00],#
	*pt++ = apu00;
	*pt++ = 0x00;

	*pt++ = 0x8f;//3 mov [01],#
	*pt++ = apu01;
	*pt++ = 0x01;

	*pt++ = 0xcd;//6 mov x,#
	*pt++ = apuSp;
	*pt++ = 0xbd;// mov sp,x

	*pt++ = 0xcd;//9 mov x,#
	*pt++ = apuPsw;
	*pt++ = 0x4d;// push x

	*pt++ = 0x8f;//12 mov [f0],#0a
	*pt++ = 0x0a;
	*pt++ = 0xf0;

	*pt++ = 0x8f;//15 mov [f1],#
	pt++;
	*pt++ = 0xf1;

	*pt++ = 0xe8;//18 mov a,#
	*pt++ = apuA;

	*pt++ = 0xcd;//20 mov x,#
	*pt++ = apuX;

	*pt++ = 0x8d;//22 mov y,#
	*pt++ = apuY;

	*pt++ = 0x8f;//24 mov [f2],6c
	*pt++ = 0x6c;
	*pt++ = 0xf2;

	*pt++ = 0x8f;//27 mov [f3],#
	pt++;
	*pt++ = 0xf3;

	*pt++ = 0x8e;// pop psw

	*pt++ = 0x5f;// jmp #
	*pt++ = apuPc.bytes[0];
	*pt++ = apuPc.bytes[1];
}

uint8_t apu4c;

void apuInitDsp(void)
{
	uint8_t i,c;
	
	for(i=0;i<128;i++)
	{
		c = sdReadOneByteFromMultiBlock();
		if(i==0x6c)
		{
			apuInitCode[28] = c;
		}
		else if(i==0x4c)
		{
			apu4c = c;
		}
		else /*if(i!=0x4c)*/
		{
			if(i==0x5c)
				c = 0xff;
			apuInitCode[0] = i;
			apuInitCode[1] = c;
			apuCopyBlock(1,0x00f2,2);
		}
	}
}

void apuExec(void)
{
	while(apuRead(0)!=0xaa);
	//while(apuRead(1)!=0xbb);
	apuWrite(2,0x70);
	apuWrite(3,0xff);
	apuWrite(1,0);
	apuWrite(0,0xcc);
	while(apuRead(0)!=0xcc);
}

#define PLAY_TIME 24000

int main(void)
{
	uint8_t apuPort[4],trackNum,cnt,apuTimer[3];
	uint16_t i;

	//DDRA = 0b00000000;
	DDRB = 0b11011111;
	//DDRD = 0b00000000;
	//PORTA = 0b00000000;
	//PORTB = 0b00000000;
	PORTD = 0b00000010;
	
	sdInit();
	
	EEAR = 0;
	EECR |= (1<<EERE);
	trackNum = 1;//EEDR;
	while(1){
		apuReset();
		sdInit();
		fat16Init();
		if(fat16FirstSearchFile(trackNum)==1){
			sdInitReadMultiBlock();

			apuInitCode[0] = 0x6c;
			apuInitCode[1] = 0x60;
			apuCopyBlock(1,0x00f2,2);
	
			sdSkipNByteFromMultiBlock(0x8000);
			sdSkipNByteFromMultiBlock(0x8100);
			apuInitDsp();
			sdStopReadMultiBlock();

			fat16FirstSearchFile(trackNum);
			sdInitReadMultiBlock();
			makeInitCode();
			apuCopyBlock(0,0x0002,0x00f0-2);
			sdReadOneByteFromMultiBlock();	// address f0 skip 
			apuInitCode[16] = (sdReadOneByteFromMultiBlock() & 0x07)|0x80;
			sdSkipNByteFromMultiBlock(2);
			apuPort[0] = sdReadOneByteFromMultiBlock();
			apuPort[1] = sdReadOneByteFromMultiBlock();
			apuPort[2] = sdReadOneByteFromMultiBlock();
			apuPort[3] = sdReadOneByteFromMultiBlock();
			sdSkipNByteFromMultiBlock(2);
			apuTimer[0] = sdReadOneByteFromMultiBlock();
			apuTimer[1] = sdReadOneByteFromMultiBlock();
			apuTimer[2] = sdReadOneByteFromMultiBlock();
			
			apuCopyBlock(0,0x00fd,0xffc0-0xfd);
			apuCopyBlock(1,0xff70,0x0016+12);
			
			apuInitCode[0] = apuTimer[0];
			apuInitCode[1] = apuTimer[1];
			apuInitCode[2] = apuTimer[2];
			apuCopyBlock(1,0x00fa,3);
			apuInitCode[0] = 0x6c;
			apuInitCode[1] = apuInitCode[28];
			apuCopyBlock(1,0x00f2,2);
			apuInitCode[0] = 0x4c;
			apuInitCode[1] = apu4c;
			apuCopyBlock(1,0x00f2,2);
			//_delay_ms(300);
			
			apuExec();
			apuWrite(0,apuPort[0]);
			apuWrite(1,apuPort[1]);
			apuWrite(2,apuPort[2]);
			apuWrite(3,apuPort[3]);

			while((PIND&0x02)==0);
			for(cnt=0,i=PLAY_TIME;i>0;i--)
			{
				if ((PIND&0x02)==0){
					if(cnt != 200)
						++cnt;
					else {
						apuReset();
						while((PIND&0x02)==0);
						_delay_ms(1000);
						while((PIND&0x02)!=0);
						break;
					}
				}
				else
				{
					if (cnt>50)
					{
						if(--trackNum==0)
							trackNum = 1;
						break;
					} else if(cnt>3 || i==1){
						++trackNum;
						break;
					}
					cnt = 0;
				}
				_delay_ms(10);
			}
		}
		else
			trackNum = 1;
		EECR = 0b00000000;
		EEDR = trackNum;
		EECR |= 1<<EEMPE;
		EECR |= 1<<EEPE;
		sdStopReadMultiBlock();
		sdCsDeSelect();
	}
}

