# Context switching code for i386 (32-bit)
# void context_switch(context_t* old_context, context_t* new_context)
#
# Parameters (on stack - cdecl calling convention):
#   [esp+4] - pointer to old context structure to save current state
#   [esp+8] - pointer to new context structure to load
#
# Context structure layout (32-bit, 4 bytes each):
#   offset 0:  edi
#   offset 4:  esi
#   offset 8:  ebx
#   offset 12: ebp
#   offset 16: esp
#   offset 20: eip
#   offset 24: eflags

.global context_switch
.type context_switch, @function

context_switch:
    # Get parameters from stack
    movl 4(%esp), %eax          # old_context pointer
    movl 8(%esp), %edx          # new_context pointer

    # Check if old_context is NULL (initial switch)
    testl %eax, %eax
    jz load_new_context

    # Save callee-saved registers to old context
    movl %edi, 0(%eax)          # edi
    movl %esi, 4(%eax)          # esi
    movl %ebx, 8(%eax)          # ebx
    movl %ebp, 12(%eax)         # ebp

    # Save stack pointer
    movl %esp, 16(%eax)         # esp

    # Save return address (instruction pointer)
    movl (%esp), %ecx           # Get return address from stack
    movl %ecx, 20(%eax)         # eip

    # Save EFLAGS
    pushfl
    popl %ecx
    movl %ecx, 24(%eax)         # eflags

load_new_context:
    # Load new context
    # Restore callee-saved registers
    movl 0(%edx), %edi          # edi
    movl 4(%edx), %esi          # esi
    movl 8(%edx), %ebx          # ebx
    movl 12(%edx), %ebp         # ebp

    # Restore EFLAGS
    movl 24(%edx), %ecx         # eflags
    pushl %ecx
    popfl

    # Restore stack pointer
    movl 16(%edx), %esp         # esp

    # Jump to new context (simulate return)
    movl 20(%edx), %eax         # eip
    jmp *%eax

# Entry point for new processes
.global process_entry_trampoline
.type process_entry_trampoline, @function
.global process_user_entry_trampoline
.type process_user_entry_trampoline, @function
.global interrupt_return_trampoline
.type interrupt_return_trampoline, @function

process_entry_trampoline:
    # Entry point address is in edi (context struct offset 0).
    # Whoever sets up the initial context must store the entry point there.
    call *%edi

    # Process returned, call exit
    pushl $0                    # Exit status 0
    call process_exit
    addl $4, %esp               # Clean up (though we won't get here)

    # Should never reach here
1:  hlt
    jmp 1b

process_user_entry_trampoline:
    call process_enter_user_mode

1:  hlt
    jmp 1b

interrupt_return_trampoline:
    pop %gs
    pop %fs
    pop %es
    pop %ds
    popal
    addl $8, %esp
    iret
