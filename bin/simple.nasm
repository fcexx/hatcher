section .text
global _start

_start:
    mov rax, 0      ; syscall number 0
    int 0x80        ; trigger syscall
    
    mov rax, 60     ; sys_exit
    mov rdi, 0      ; exit code 0
    int 0x80        ; exit