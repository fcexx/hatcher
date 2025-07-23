#include <port_based.h>

unsigned char   inb(unsigned short port)
{
    unsigned char result;
	__asm__("in %%dx, %%al" : "=a" (result) : "d" (port));
    return (result);
}


void    outb(unsigned short port, unsigned char data)
{
	__asm__("out %%al, %%dx" : : "a" (data), "d" (port));
}


unsigned short   inw(unsigned short port)
{
    unsigned short result;
    __asm__("in %%dx, %%ax" : "=a" (result) : "d" (port));
    return (result);
}


void outw(unsigned short port, unsigned short data)
{
    __asm__("out %%ax, %%dx" : : "a" (data), "d" (port));
}

uint32_t inl(unsigned short port)
{
    uint32_t result;
    __asm__("inl %%dx, %%eax" : "=a" (result) : "d" (port));
    return result;
}

void outl(unsigned short port, uint32_t data)
{
    __asm__("outl %%eax, %%dx" : : "a" (data), "d" (port));
}

void insw(unsigned short port, void *buf, unsigned int count)
{
    __asm__("rep insw" : "+D" (buf), "+c" (count) : "d" (port));
}

void outsw(unsigned short port, void *buf, unsigned int count)
{
    __asm__("rep outsw" : "+D" (buf), "+c" (count) : "d" (port));
}