#include <stdio.h>
#include <stdint.h>
#include <signal.h>
/* windows only */
#include <Windows.h>
#include <conio.h>  // _kbhit

/* Registers */
enum 
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,
    R_COND,
    R_COUNT
};

/* Condition Flags*/
enum 
{
    FL_POS = 1 << 0,
    FL_ZRO = 1 << 1,
    FL_NEG = 1 << 2,
};

/* Opcodes */
enum 
{
    OP_BR = 0,
    OP_ADD,
    OP_LD,  //load
    OP_ST,  //store
    OP_JSR, //jump register
    OP_AND, //bitwise and
    OP_LDR, //load register
    OP_STR, //store register
    OP_RTI, //unused
    OP_NOT, //bitwise not
    OP_LDI, //load indirect
    OP_STI, //store indirect
    OP_JMP, //jump
    OP_RES, //reserved
    OP_LEA, //load effective address
    OP_TRAP //execute trap
};

enum 
{
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
};

enum 
{
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};



/* Memory */
#define MEMORY_MAX (1 << 16)
uint16_t memory[MEMORY_MAX];
uint16_t reg[R_COUNT];


HANDLE hStdin = INVALID_HANDLE_VALUE;
DWORD fdwMode, fdwOldMode;

void disable_input_buffering()
{
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &fdwOldMode); /* save old mode */
    fdwMode = fdwOldMode
            ^ ENABLE_ECHO_INPUT  /* no input echo */
            ^ ENABLE_LINE_INPUT; /* return when one or
                                    more characters are available */
    SetConsoleMode(hStdin, fdwMode); /* set new mode */
    FlushConsoleInputBuffer(hStdin); /* clear buffer */
}

void restore_input_buffering()
{
    SetConsoleMode(hStdin, fdwOldMode);
}

uint16_t check_key()
{
    return WaitForSingleObject(hStdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15) /* a 1 in the left-most bit indicates negative */
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}

void read_image_file(FILE* file)
{
    /* the origin tells us where in memory to place the image */
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    /* we know the maximum file size so we only need one fread */
    uint16_t max_read = MEMORY_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    /* swap to little endian */
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}


int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if (!file) { return 0; };
    read_image_file(file);
    fclose(file);
    return 1;
}

void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = (1 << 15);
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }
    }
    return memory[address];
}


int main(int argc, const char* argv[])
{
    if (argc < 2)
    {
        /* Show usage string*/
        printf("lc3 [image-file1] ...\n");
        exit(2);
    }

    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("Failed to load image: %s\n", argv[j]);
            exit(1);
        }
    }
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    /* Only 1 condition flag should be set at any given time*/
    reg[R_COND]=FL_ZRO;

    //Starting PC is 0x3000
    enum { PC_START = 0x3000};
    reg[R_PC] = PC_START;

    int running = 1;
    while(running)
    {
        /* FETCH */
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch (op)
        {
            case OP_ADD:
                {
                    /* destination register (DR) */
                uint16_t r0 = (instr >> 9) & 0x7;
                /* first operand (SR1) */
                uint16_t r1 = (instr >> 6) & 0x7;
                /* whether we are in immediate mode */
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag)
                {
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[r0] = reg[r1] + imm5;
                }
                else
                {
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] + reg[r2];
                }

                update_flags(r0);
                break;
                }

            case OP_AND:
            {/* Parsing for registers and immediate flag*/
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                uint16_t imm_flag = (instr >> 5) & 0x1;

                if (imm_flag)
                {
                    /* Extend the immediate and store the result of AND into r0*/
                    uint16_t imm5 = sign_extend(instr & 0x1F, 5);
                    reg[r0] = reg[r1] & imm5;
                }
                else
                {
                    /* Find the value at r2 and store result of AND into r0*/
                    uint16_t r2 = instr & 0x7;
                    reg[r0] = reg[r1] & reg[r2];
                }
                update_flags(r0);
                break;}


            case OP_NOT:
            {/* Parse for registers r0 and r1*/
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;

                /* Perform bitwise NOT and store*/
                reg[r0] = ~reg[r1];
                update_flags(r0);
                break;}


            case OP_BR:
            {/* Parse for the immediate to offset by and the condition flag*/
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                uint16_t cond_flag = (instr >> 9) & 0x7;
                if (cond_flag & reg[R_COND])
                {
                    /* Shift PC by the offset*/
                    reg[R_PC] += pc_offset;
                }
                break;}


            case OP_JMP:
                {/* Set PC to a value within a register. 
                Also handles RET. Takes place when R1 is 7 */
                uint16_t r1 = (instr >> 6) & 0x7;
                reg[R_PC] = reg[r1];
                break;}


            case OP_JSR:
            {/* Parse for the long flag. 
            If true, get the immediate and use as the offset of PC.
            Else, get r1 and set PC to that value in the register.
            Differs due to R_R7 holding the previous PC.*/
                uint16_t long_flag = (instr >> 11) & 1;
                reg[R_R7] = reg[R_PC];
                if (long_flag)
                {
                    uint16_t long_pc_offset = sign_extend(instr & 0x7FF, 11);
                    reg[R_PC] += long_pc_offset;  /* JSR */
                }
                else
                {
                    uint16_t r1 = (instr >> 6) & 0x7;
                    reg[R_PC] = reg[r1]; /* JSRR */
                }
                break;}


            case OP_LD: /* Read from this register offset*/
                {uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                reg[r0] = mem_read(reg[R_PC] + pc_offset);
                update_flags(r0);
                break;}


            case OP_LDI: /* Read from this register within the register offset*/
                {/* destination register (DR) */
                uint16_t r0 = (instr >> 9) & 0x7;
                /* PCoffset 9*/
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                /* add pc_offset to the current PC, look at that memory location to get the final address */
                reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                update_flags(r0);
                break;}


            case OP_LDR:
            {/* Parse for registers. Find the offset. load the value of whats in the register + offset.*/
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                uint16_t offset = sign_extend(instr & 0x3F, 6);
                reg[r0] = mem_read(reg[r1] + offset);
                update_flags(r0);
                break;}


            case OP_LEA:
            {/* Parse for register. Find offset. Load the value of the PC + offset.*/
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                reg[r0] = reg[R_PC] + pc_offset;
                update_flags(r0);
                break;}

                
            case OP_ST:
            {/* Parse for register and offset. Write the value of the register to the address [PC + offset]*/
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                mem_write(reg[R_PC] + pc_offset, reg[r0]);
                break;}


            case OP_STI:
            {/* Parse for register & offset. Write the value of register 0 into the address 
            stored by the location [PC + offset]. Here, PC+ offset directs to a different address.*/
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
                mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
                break;}


            case OP_STR:
            {/* Parse for registers & offset. Write the value of r0 to the content of r1 + offset.*/
                uint16_t r0 = (instr >> 9) & 0x7;
                uint16_t r1 = (instr >> 6) & 0x7;
                uint16_t offset = sign_extend(instr & 0x3F, 6);
                mem_write(reg[r1] + offset, reg[r0]);
                break;}


            case OP_TRAP:
                reg[R_R7] = reg[R_PC];

                switch (instr & 0xFF)
                {
                    case TRAP_GETC:
                        reg[R_R0] = (uint16_t)getchar();
                        update_flags(R_R0); 
                        break;


                    case TRAP_OUT:
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                        break;


                    case TRAP_PUTS:
                        {/* one char per word */
                        uint16_t* c = memory + reg[R_R0];
                        while (*c)
                        {
                            putc((char)*c, stdout);
                            ++c;
                        }
                        fflush(stdout);}
                        break;


                    case TRAP_IN:
                        {printf("Enter a character: ");
                        char c = getchar();
                        putc(c, stdout);
                        fflush(stdout);
                        reg[R_R0] = (uint16_t)c;
                        update_flags(R_R0);}
                        break;


                    case TRAP_PUTSP:
                    {/* Because 1 char per byte, we need to use big endian*/
                        uint16_t* c = memory + reg[R_R0];
                        while (*c)
                        {
                            char char1 = (*c) & 0xFF;
                            putc(char1, stdout);
                            char char2 = (*c) >> 8;
                            if (char2) putc(char2, stdout);
                            ++c;
                        }
                        fflush(stdout);}
                        break;


                    case TRAP_HALT:
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
                        break;
                }
                break;


            case OP_RES:
                abort();
            case OP_RTI:
                abort();
            default:
                abort();
                break;
        }
    }
    restore_input_buffering();
}

