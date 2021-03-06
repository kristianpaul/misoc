#include <stdio.h>
#include <stdlib.h>
#include <console.h>
#include <string.h>
#include <uart.h>
#include <system.h>
#include <id.h>
#include <irq.h>
#include <crc.h>

#include <generated/csr.h>
#include <generated/mem.h>
#include <net/microudp.h>

#include "sdram.h"
#include "dataflow.h"
#include "boot.h"

enum {
	CSR_IE = 1, CSR_IM, CSR_IP, CSR_ICC, CSR_DCC, CSR_CC, CSR_CFG, CSR_EBA,
	CSR_DC, CSR_DEBA, CSR_JTX, CSR_JRX, CSR_BP0, CSR_BP1, CSR_BP2, CSR_BP3,
	CSR_WP0, CSR_WP1, CSR_WP2, CSR_WP3,
};

/* General address space functions */

#define NUMBER_OF_BYTES_ON_A_LINE 16
static void dump_bytes(unsigned int *ptr, int count, unsigned addr)
{
	char *data = (char *)ptr;
	int line_bytes = 0, i = 0;

	putsnonl("Memory dump:");
	while(count > 0){
		line_bytes =
			(count > NUMBER_OF_BYTES_ON_A_LINE)?
				NUMBER_OF_BYTES_ON_A_LINE : count;

		printf("\n0x%08x  ", addr);
		for(i=0;i<line_bytes;i++)
			printf("%02x ", *(unsigned char *)(data+i));

		for(;i<NUMBER_OF_BYTES_ON_A_LINE;i++)
			printf("   ");

		printf(" ");

		for(i=0;i<line_bytes;i++) {
			if((*(data+i) < 0x20) || (*(data+i) > 0x7e))
				printf(".");
			else
				printf("%c", *(data+i));
		}

		for(;i<NUMBER_OF_BYTES_ON_A_LINE;i++)
			printf(" ");

		data += (char)line_bytes;
		count -= line_bytes;
		addr += line_bytes;
	}
	printf("\n");
}

static void mr(char *startaddr, char *len)
{
	char *c;
	unsigned int *addr;
	unsigned int length;

	if(*startaddr == 0) {
		printf("mr <address> [length]\n");
		return;
	}
	addr = (unsigned *)strtoul(startaddr, &c, 0);
	if(*c != 0) {
		printf("incorrect address\n");
		return;
	}
	if(*len == 0) {
		length = 4;
	} else {
		length = strtoul(len, &c, 0);
		if(*c != 0) {
			printf("incorrect length\n");
			return;
		}
	}

	dump_bytes(addr, length, (unsigned)addr);
}

static void mw(char *addr, char *value, char *count)
{
	char *c;
	unsigned int *addr2;
	unsigned int value2;
	unsigned int count2;
	unsigned int i;

	if((*addr == 0) || (*value == 0)) {
		printf("mw <address> <value> [count]\n");
		return;
	}
	addr2 = (unsigned int *)strtoul(addr, &c, 0);
	if(*c != 0) {
		printf("incorrect address\n");
		return;
	}
	value2 = strtoul(value, &c, 0);
	if(*c != 0) {
		printf("incorrect value\n");
		return;
	}
	if(*count == 0) {
		count2 = 1;
	} else {
		count2 = strtoul(count, &c, 0);
		if(*c != 0) {
			printf("incorrect count\n");
			return;
		}
	}
	for (i=0;i<count2;i++) *addr2++ = value2;
}

static void mc(char *dstaddr, char *srcaddr, char *count)
{
	char *c;
	unsigned int *dstaddr2;
	unsigned int *srcaddr2;
	unsigned int count2;
	unsigned int i;

	if((*dstaddr == 0) || (*srcaddr == 0)) {
		printf("mc <dst> <src> [count]\n");
		return;
	}
	dstaddr2 = (unsigned int *)strtoul(dstaddr, &c, 0);
	if(*c != 0) {
		printf("incorrect destination address\n");
		return;
	}
	srcaddr2 = (unsigned int *)strtoul(srcaddr, &c, 0);
	if(*c != 0) {
		printf("incorrect source address\n");
		return;
	}
	if(*count == 0) {
		count2 = 1;
	} else {
		count2 = strtoul(count, &c, 0);
		if(*c != 0) {
			printf("incorrect count\n");
			return;
		}
	}
	for (i=0;i<count2;i++) *dstaddr2++ = *srcaddr2++;
}

static void crc(char *startaddr, char *len)
{
	char *c;
	char *addr;
	unsigned int length;

	if((*startaddr == 0)||(*len == 0)) {
		printf("crc <address> <length>\n");
		return;
	}
	addr = (char *)strtoul(startaddr, &c, 0);
	if(*c != 0) {
		printf("incorrect address\n");
		return;
	}
	length = strtoul(len, &c, 0);
	if(*c != 0) {
		printf("incorrect length\n");
		return;
	}

	printf("CRC32: %08x\n", crc32((unsigned char *)addr, length));
}

/* processor registers */
static int parse_csr(const char *csr)
{
	if(!strcmp(csr, "ie"))   return CSR_IE;
	if(!strcmp(csr, "im"))   return CSR_IM;
	if(!strcmp(csr, "ip"))   return CSR_IP;
	if(!strcmp(csr, "icc"))  return CSR_ICC;
	if(!strcmp(csr, "dcc"))  return CSR_DCC;
	if(!strcmp(csr, "cc"))   return CSR_CC;
	if(!strcmp(csr, "cfg"))  return CSR_CFG;
	if(!strcmp(csr, "eba"))  return CSR_EBA;
	if(!strcmp(csr, "dc"))   return CSR_DC;
	if(!strcmp(csr, "deba")) return CSR_DEBA;
	if(!strcmp(csr, "jtx"))  return CSR_JTX;
	if(!strcmp(csr, "jrx"))  return CSR_JRX;
	if(!strcmp(csr, "bp0"))  return CSR_BP0;
	if(!strcmp(csr, "bp1"))  return CSR_BP1;
	if(!strcmp(csr, "bp2"))  return CSR_BP2;
	if(!strcmp(csr, "bp3"))  return CSR_BP3;
	if(!strcmp(csr, "wp0"))  return CSR_WP0;
	if(!strcmp(csr, "wp1"))  return CSR_WP1;
	if(!strcmp(csr, "wp2"))  return CSR_WP2;
	if(!strcmp(csr, "wp3"))  return CSR_WP3;

	return 0;
}

static void rcsr(char *csr)
{
	unsigned int csr2;
	register unsigned int value;

	if(*csr == 0) {
		printf("rcsr <csr>\n");
		return;
	}

	csr2 = parse_csr(csr);
	if(csr2 == 0) {
		printf("incorrect csr\n");
		return;
	}

	switch(csr2) {
		case CSR_IE:   asm volatile ("rcsr %0,ie":"=r"(value)); break;
		case CSR_IM:   asm volatile ("rcsr %0,im":"=r"(value)); break;
		case CSR_IP:   asm volatile ("rcsr %0,ip":"=r"(value)); break;
		case CSR_CC:   asm volatile ("rcsr %0,cc":"=r"(value)); break;
		case CSR_CFG:  asm volatile ("rcsr %0,cfg":"=r"(value)); break;
		case CSR_EBA:  asm volatile ("rcsr %0,eba":"=r"(value)); break;
		case CSR_DEBA: asm volatile ("rcsr %0,deba":"=r"(value)); break;
		case CSR_JTX:  asm volatile ("rcsr %0,jtx":"=r"(value)); break;
		case CSR_JRX:  asm volatile ("rcsr %0,jrx":"=r"(value)); break;
		default: printf("csr write only\n"); return;
	}

	printf("%08x\n", value);
}

static void wcsr(char *csr, char *value)
{
	char *c;
	unsigned int csr2;
	register unsigned int value2;

	if((*csr == 0) || (*value == 0)) {
		printf("wcsr <csr> <address>\n");
		return;
	}

	csr2 = parse_csr(csr);
	if(csr2 == 0) {
		printf("incorrect csr\n");
		return;
	}
	value2 = strtoul(value, &c, 0);
	if(*c != 0) {
		printf("incorrect value\n");
		return;
	}

	switch(csr2) {
		case CSR_IE:   asm volatile ("wcsr ie,%0"::"r"(value2)); break;
		case CSR_IM:   asm volatile ("wcsr im,%0"::"r"(value2)); break;
		case CSR_ICC:  asm volatile ("wcsr icc,%0"::"r"(value2)); break;
		case CSR_DCC:  asm volatile ("wcsr dcc,%0"::"r"(value2)); break;
		case CSR_EBA:  asm volatile ("wcsr eba,%0"::"r"(value2)); break;
		case CSR_DC:   asm volatile ("wcsr dcc,%0"::"r"(value2)); break;
		case CSR_DEBA: asm volatile ("wcsr deba,%0"::"r"(value2)); break;
		case CSR_JTX:  asm volatile ("wcsr jtx,%0"::"r"(value2)); break;
		case CSR_JRX:  asm volatile ("wcsr jrx,%0"::"r"(value2)); break;
		case CSR_BP0:  asm volatile ("wcsr bp0,%0"::"r"(value2)); break;
		case CSR_BP1:  asm volatile ("wcsr bp1,%0"::"r"(value2)); break;
		case CSR_BP2:  asm volatile ("wcsr bp2,%0"::"r"(value2)); break;
		case CSR_BP3:  asm volatile ("wcsr bp3,%0"::"r"(value2)); break;
		case CSR_WP0:  asm volatile ("wcsr wp0,%0"::"r"(value2)); break;
		case CSR_WP1:  asm volatile ("wcsr wp1,%0"::"r"(value2)); break;
		case CSR_WP2:  asm volatile ("wcsr wp2,%0"::"r"(value2)); break;
		case CSR_WP3:  asm volatile ("wcsr wp3,%0"::"r"(value2)); break;
		default: printf("csr read only\n"); return;
	}
}

static void dfs(char *baseaddr)
{
	char *c;
	unsigned int addr;

	if(*baseaddr == 0) {
		printf("dfs <address>\n");
		return;
	}
	addr = strtoul(baseaddr, &c, 0);
	if(*c != 0) {
		printf("incorrect address\n");
		return;
	}
	print_isd_info(addr);
}

/* Init + command line */

static void help(void)
{
	puts("MiSoC BIOS");
	puts("Available commands:");
	puts("mr         - read address space");
	puts("mw         - write address space");
	puts("mc         - copy address space");
	puts("crc        - compute CRC32 of a part of the address space");
	puts("rcsr       - read processor CSR");
	puts("wcsr       - write processor CSR");
#ifdef MINIMAC_BASE
	puts("netboot    - boot via TFTP");
#endif
	puts("serialboot - boot via SFL");
#ifdef FLASH_BOOT_ADDRESS
	puts("flashboot  - boot from flash");
#endif
	puts("revision   - display revision");
}

static char *get_token(char **str)
{
	char *c, *d;

	c = (char *)strchr(*str, ' ');
	if(c == NULL) {
		d = *str;
		*str = *str+strlen(*str);
		return d;
	}
	*c = 0;
	d = *str;
	*str = c+1;
	return d;
}

static void do_command(char *c)
{
	char *token;

	token = get_token(&c);

	if(strcmp(token, "mr") == 0) mr(get_token(&c), get_token(&c));
	else if(strcmp(token, "mw") == 0) mw(get_token(&c), get_token(&c), get_token(&c));
	else if(strcmp(token, "mc") == 0) mc(get_token(&c), get_token(&c), get_token(&c));
	else if(strcmp(token, "crc") == 0) crc(get_token(&c), get_token(&c));
	else if(strcmp(token, "flushl2") == 0) flush_l2_cache();

#ifdef FLASH_BOOT_ADDRESS
	else if(strcmp(token, "flashboot") == 0) flashboot();
#endif
	else if(strcmp(token, "serialboot") == 0) serialboot();
#ifdef MINIMAC_BASE
	else if(strcmp(token, "netboot") == 0) netboot();
#endif
	
	else if(strcmp(token, "revision") == 0) printf("%08x\n", MSC_GIT_ID);

	else if(strcmp(token, "help") == 0) help();

	else if(strcmp(token, "rcsr") == 0) rcsr(get_token(&c));
	else if(strcmp(token, "wcsr") == 0) wcsr(get_token(&c), get_token(&c));
	
#ifdef DFII_BASE
	else if(strcmp(token, "ddrrow") == 0) ddrrow(get_token(&c));
	else if(strcmp(token, "ddrsw") == 0) ddrsw();
	else if(strcmp(token, "ddrhw") == 0) ddrhw();
	else if(strcmp(token, "ddrrd") == 0) ddrrd(get_token(&c));
	else if(strcmp(token, "ddrwr") == 0) ddrwr(get_token(&c));
	else if(strcmp(token, "memtest") == 0) memtest();
	else if(strcmp(token, "ddrinit") == 0) ddrinit();
#endif
	
	else if(strcmp(token, "dfs") == 0) dfs(get_token(&c));

	else if(strcmp(token, "") != 0)
		printf("Command not found\n");
}

extern unsigned int _ftext, _edata;

static void crcbios(void)
{
	unsigned int offset_bios;
	unsigned int length;
	unsigned int expected_crc;
	unsigned int actual_crc;

	/*
	 * _edata is located right after the end of the flat
	 * binary image. The CRC tool writes the 32-bit CRC here.
	 * We also use the address of _edata to know the length
	 * of our code.
	 */
	offset_bios = (unsigned int)&_ftext;
	expected_crc = _edata;
	length = (unsigned int)&_edata - offset_bios;
	actual_crc = crc32((unsigned char *)offset_bios, length);
	if(expected_crc == actual_crc)
		printf("BIOS CRC passed (%08x)\n", actual_crc);
	else {
		printf("BIOS CRC failed (expected %08x, got %08x)\n", expected_crc, actual_crc);
		printf("The system will continue, but expect problems.\n");
	}
}

static void readstr(char *s, int size)
{
	char c[2];
	int ptr;

	c[1] = 0;
	ptr = 0;
	while(1) {
		c[0] = readchar();
		switch(c[0]) {
			case 0x7f:
			case 0x08:
				if(ptr > 0) {
					ptr--;
					putsnonl("\x08 \x08");
				}
				break;
			case 0x07:
				break;
			case '\r':
			case '\n':
				s[ptr] = 0x00;
				putsnonl("\n");
				return;
			default:
				putsnonl(c);
				s[ptr] = c[0];
				ptr++;
				break;
		}
	}
}

static int test_user_abort(void)
{
	char c;

	printf("Automatic boot in 2 seconds...\n");
	printf("Q/ESC: abort boot\n");
	printf("F7:    boot from serial\n");
#ifdef MINIMAC_BASE
	printf("F8:    boot from network\n");
#endif
	timer0_en_write(0);
	timer0_reload_write(0);
	timer0_load_write(identifier_frequency_read()*2);
	timer0_en_write(1);
	timer0_update_value_write(1);
	while(timer0_value_read()) {
		if(readchar_nonblock()) {
			c = readchar();
			if((c == 'Q')||(c == '\e')) {
				puts("Aborted");
				return 0;
			}
			if(c == 0x06) {
				serialboot();
				return 0;
			}
#ifdef MINIMAC_BASE
			if(c == 0x07) {
				netboot();
				return 0;
			}
#endif
		}
		timer0_update_value_write(1);
	}
	return 1;
}

static void boot_sequence(void)
{
	if(test_user_abort()) {
#ifdef FLASH_BOOT_ADDRESS
		flashboot();
#endif
		serialboot();
#ifdef MINIMAC_BASE
		netboot();
#endif
		printf("No boot medium found\n");
	}
}

int main(int i, char **c)
{
	char buffer[64];
	int ddr_ok;

	irq_setmask(0);
	irq_setie(1);
	uart_init();
	puts("\nMiSoC BIOS   http://m-labs.hk\n"
	"(c) Copyright 2007-2014 Sebastien Bourdeauducq");
	printf("Revision %08x built "__DATE__" "__TIME__"\n\n", MSC_GIT_ID);
	crcbios();
	id_print();
#ifdef MINIMAC_BASE
	ethreset();
#endif
#ifdef DFII_BASE
	ddr_ok = ddrinit();
#else
	ddr_ok = 1;
#endif
	if(ddr_ok)
		boot_sequence();
	else
		printf("Memory initialization failed\n");
	
	while(1) {
		putsnonl("\e[1mBIOS>\e[0m ");
		readstr(buffer, 64);
		do_command(buffer);
	}
	return 0;
}
