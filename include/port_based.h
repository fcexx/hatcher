#ifndef PORT_BASED_H
#define PORT_BASED_H

#include <stdint.h>

unsigned char   inb(unsigned short port);
void    outb(unsigned short port, unsigned char data);
unsigned short inw(unsigned short port);
void outw(unsigned short port, unsigned short data);

uint32_t inl(unsigned short port);
void outl(unsigned short port, uint32_t data);

void insw(unsigned short port, void *buf, unsigned int count);
void outsw(unsigned short port, void *buf, unsigned int count);

#endif // PORT_BASED_H