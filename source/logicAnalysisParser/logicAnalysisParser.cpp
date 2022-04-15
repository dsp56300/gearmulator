#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int readint(FILE *f)
{
	int a=fgetc(f)&255;
	int b=fgetc(f)&255;
	return (b<<8)|a;
}

int main(int _argc, char* _argv[])
{
	if(_argc < 3)
	{
		printf("Usae: infile outfile\n");
		getchar();
		return -1;
	}
	FILE *f=fopen(_argv[1],"rb");
	FILE *fw=fopen(_argv[2],"wb");
	int ale=0,offset=-1,address=0,data=0,lastx=0;
	int ICR=0,ISR=0,TXa=0,TXb=0,TXc=0,justWritten=false;
	while (!feof(f)) {
		offset++;
		int x=readint(f);
		if (x==lastx) continue;

		if (!(x&256) && (lastx&256)) address=x&0xff;
		
		if (x&512)
		{
			if ((x&2048) && !(lastx&2048))
			{
				int data=lastx&255;
				switch (address&7)
				{
					case 0:	ICR=data;	break;
					case 2: ISR=data;	break;
					case 5: TXa=data;	break;
					case 6: TXb=data;	break;
					case 7:	TXc=data;	fputc(TXc,fw);fputc(TXb,fw);fputc(TXa,fw);break;
				}
				printf("%08x Write:  %02x at %02x\n",offset*2,lastx&255,address);
			}
			if ((x&1024) && !(lastx&1024)) printf("%08x Read: %02x at %02x\n",offset*2,x&255,address);
		}
		
		lastx=x;
	}
	fclose(f);
	fclose(fw);
	return 0;
}
